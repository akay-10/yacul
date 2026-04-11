#include "yacul/system/system_info.h"

#include "yacul/system/popen_wrapper.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <ifaddrs.h>
#include <iomanip>
#include <net/if.h>
#include <netinet/in.h>
#include <pwd.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utility>
#include <utmp.h>

using namespace std;

// POSIX: environment pointer, declared at global scope to avoid linker errors
// that arise from declaring it inside a namespace.
extern char **environ; // NOLINT(readability-redundant-declaration)

namespace utils {
namespace system {

// ---------------------------------------------------------------------------

uint64_t SystemInfo::CpuCoreStats::Total() const {
  return user + nice + system_ticks + idle + iowait + irq + softirq + steal +
         guest + guest_nice;
}

// ---------------------------------------------------------------------------

uint64_t SystemInfo::CpuCoreStats::Busy() const {
  return user + nice + system_ticks + irq + softirq + steal;
}

// ---------------------------------------------------------------------------

double SystemInfo::CpuCoreStats::UsagePercent(const CpuCoreStats &prev) const {
  // Guard against counter wrap or stale snapshots.
  if (Total() < prev.Total())
    return 0.0;
  uint64_t delta_total = Total() - prev.Total();
  if (delta_total == 0)
    return 0.0;
  uint64_t prev_busy = prev.Busy();
  uint64_t cur_busy = Busy();
  if (cur_busy < prev_busy)
    return 0.0;
  uint64_t delta_busy = cur_busy - prev_busy;
  return 100.0 * static_cast<double>(delta_busy) /
         static_cast<double>(delta_total);
}

// ---------------------------------------------------------------------------

double SystemInfo::MemoryInfo::UsedPercent() const {
  if (total == 0)
    return 0.0;
  return 100.0 * static_cast<double>(Used()) / static_cast<double>(total);
}

// ---------------------------------------------------------------------------

double SystemInfo::MemoryInfo::SwapUsedPercent() const {
  if (swap_total == 0)
    return 0.0;
  return 100.0 * static_cast<double>(SwapUsed()) /
         static_cast<double>(swap_total);
}

// ---------------------------------------------------------------------------

double SystemInfo::DiskPartition::UsedPercent() const {
  if (total == 0)
    return 0.0;
  return 100.0 * static_cast<double>(used) / static_cast<double>(total);
}

// ---------------------------------------------------------------------------

double SystemInfo::DiskPartition::InodesUsedPercent() const {
  if (inodes_total == 0)
    return 0.0;
  return 100.0 * static_cast<double>(inodes_used) /
         static_cast<double>(inodes_total);
}

// ---------------------------------------------------------------------------

SystemInfo::SystemInfo() : SystemInfo(Options{}) {}

// ---------------------------------------------------------------------------

SystemInfo::SystemInfo(Options options)
  : impl_(make_unique<Impl>(move(options))) {
  impl_->prev_cpu_time = chrono::steady_clock::now();
  // Warm the CPU baseline immediately so the first delta is meaningful.
  impl_->prev_cpu_stats = ParseProcStat();

  if (impl_->opts.enable_background_refresh) {
    StartBackgroundRefresh();
  }
}

// ---------------------------------------------------------------------------

// Move ctor/assign: transfer the unique_ptr; StopBackgroundRefresh is called
// in the destructor of the moved-from object's Impl (if thread is running,
// we stop it first to avoid dangling 'this' references).
SystemInfo::SystemInfo(SystemInfo &&other) noexcept {
  if (other.impl_) {
    other.StopBackgroundRefresh();
  }
  impl_ = move(other.impl_);
}

// ---------------------------------------------------------------------------

SystemInfo &SystemInfo::operator=(SystemInfo &&other) noexcept {
  if (this != &other) {
    StopBackgroundRefresh();
    if (other.impl_) {
      other.StopBackgroundRefresh();
    }
    impl_ = move(other.impl_);
  }
  return *this;
}

// ---------------------------------------------------------------------------

SystemInfo::~SystemInfo() {
  if (impl_) {
    StopBackgroundRefresh();
  }
}

// ---------------------------------------------------------------------------

void SystemInfo::InvalidateCache() {
  InvalidateCache("os");
  InvalidateCache("cpu");
  InvalidateCache("memory");
  InvalidateCache("disk");
  InvalidateCache("disk_io");
  InvalidateCache("network");
  InvalidateCache("thermal");
  InvalidateCache("gpu");
  InvalidateCache("processes");
  InvalidateCache("load");
  InvalidateCache("docker");
}

// ---------------------------------------------------------------------------

void SystemInfo::InvalidateCache(const string &domain) {
  if (domain == "os") {
    lock_guard<mutex> lk(impl_->os_mutex);
    impl_->os_cache.valid = false;
  } else if (domain == "cpu") {
    lock_guard<mutex> lk(impl_->cpu_mutex);
    impl_->cpu_cache.valid = false;
  } else if (domain == "memory") {
    lock_guard<mutex> lk(impl_->mem_mutex);
    impl_->mem_cache.valid = false;
  } else if (domain == "disk") {
    lock_guard<mutex> lk(impl_->disk_mutex);
    impl_->disk_cache.valid = false;
  } else if (domain == "disk_io") {
    lock_guard<mutex> lk(impl_->disk_io_mutex);
    impl_->disk_io_cache.valid = false;
  } else if (domain == "network") {
    lock_guard<mutex> lk(impl_->net_mutex);
    impl_->net_cache.valid = false;
  } else if (domain == "thermal") {
    lock_guard<mutex> lk(impl_->thermal_mutex);
    impl_->thermal_cache.valid = false;
  } else if (domain == "gpu") {
    lock_guard<mutex> lk(impl_->gpu_mutex);
    impl_->gpu_cache.valid = false;
  } else if (domain == "processes") {
    lock_guard<mutex> lk(impl_->proc_mutex);
    impl_->proc_cache.valid = false;
  } else if (domain == "load") {
    lock_guard<mutex> lk(impl_->load_mutex);
    impl_->load_cache.valid = false;
  } else if (domain == "docker") {
    lock_guard<mutex> lk(impl_->docker_mutex);
    impl_->docker_cache.valid = false;
  }
}

// ---------------------------------------------------------------------------

void SystemInfo::StartBackgroundRefresh() {
  bool expected = false;
  if (!impl_->refresh_running.compare_exchange_strong(expected, true))
    return;
  impl_->refresh_thread = thread(&SystemInfo::RefreshLoop, this);
}

// ---------------------------------------------------------------------------

void SystemInfo::StopBackgroundRefresh() {
  if (!impl_)
    return;
  impl_->refresh_running.store(false);
  if (impl_->refresh_thread.joinable())
    impl_->refresh_thread.join();
}

// ---------------------------------------------------------------------------

bool SystemInfo::IsBackgroundRefreshRunning() const {
  return impl_->refresh_running.load();
}

// ---------------------------------------------------------------------------

void SystemInfo::RefreshLoop() {
  while (impl_->refresh_running.load()) {
    this_thread::sleep_for(impl_->opts.refresh_interval);
    if (!impl_->refresh_running.load())
      break;
    try {
      {
        lock_guard<mutex> lk(impl_->cpu_mutex);
        impl_->cpu_cache.Set(FetchCpuInfo());
      }
      {
        lock_guard<mutex> lk(impl_->mem_mutex);
        impl_->mem_cache.Set(FetchMemoryInfo());
      }
      {
        lock_guard<mutex> lk(impl_->net_mutex);
        impl_->net_cache.Set(FetchNetworkInfo());
      }
      {
        lock_guard<mutex> lk(impl_->load_mutex);
        impl_->load_cache.Set(FetchLoadAverage());
      }
    } catch (...) {
      // Best-effort: never let exceptions kill the background thread.
    }
  }
}

// ---------------------------------------------------------------------------

string SystemInfo::ReadFile(const string &path) {
  ifstream f(path);
  if (!f.is_open())
    return {};
  ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// ---------------------------------------------------------------------------

uint64_t SystemInfo::ReadUint64File(const string &path,
                                    uint64_t default_value) {
  string content = ReadFile(path);
  if (content.empty())
    return default_value;
  try {
    return stoull(content);
  } catch (...) {
    return default_value;
  }
}

// ---------------------------------------------------------------------------

bool SystemInfo::ExecutableExists(const string &name) {
  if (!name.empty() && name.front() == '/') {
    struct stat st;
    return (stat(name.c_str(), &st) == 0) && (st.st_mode & S_IXUSR);
  }
  const char *path_env = getenv("PATH");
  if (!path_env)
    return false;
  istringstream ss(path_env);
  string dir;
  while (getline(ss, dir, ':')) {
    string full = dir + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) == 0 && (st.st_mode & S_IXUSR))
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

string SystemInfo::FormatBytes(uint64_t bytes) {
  constexpr double kKib = 1024.0;
  constexpr double kMib = kKib * 1024.0;
  constexpr double kGib = kMib * 1024.0;
  constexpr double kTib = kGib * 1024.0;
  constexpr double kPib = kTib * 1024.0;

  ostringstream ss;
  ss << fixed << setprecision(2);
  double b = static_cast<double>(bytes);
  if (b >= kPib)
    ss << b / kPib << " PiB";
  else if (b >= kTib)
    ss << b / kTib << " TiB";
  else if (b >= kGib)
    ss << b / kGib << " GiB";
  else if (b >= kMib)
    ss << b / kMib << " MiB";
  else if (b >= kKib)
    ss << b / kKib << " KiB";
  else
    ss << bytes << " B";
  return ss.str();
}

// ---------------------------------------------------------------------------

string SystemInfo::FormatUptime(chrono::seconds uptime) {
  long long s = uptime.count();
  long days = static_cast<long>(s / 86400);
  s %= 86400;
  long hours = static_cast<long>(s / 3600);
  s %= 3600;
  long mins = static_cast<long>(s / 60);
  s %= 60;
  ostringstream ss;
  if (days > 0)
    ss << days << "d ";
  if (hours > 0)
    ss << hours << "h ";
  if (mins > 0)
    ss << mins << "m ";
  ss << s << "s";
  return ss.str();
}

// ---------------------------------------------------------------------------

string SystemInfo::FormatPercent(double pct, int decimals) {
  ostringstream ss;
  ss << fixed << setprecision(decimals) << pct << "%";
  return ss.str();
}

// ---------------------------------------------------------------------------

string SystemInfo::RunCommand(const vector<string> &argv) {
  PopenWrapper::Options pop_opts;
  pop_opts.command = argv;
  pop_opts.mode = PopenWrapper::Mode::kReadOnly;
  pop_opts.capture_stderr = false;
  pop_opts.timeout = impl_->opts.command_timeout;

  PopenWrapper proc(pop_opts);
  auto result = proc.Run();
  if (!result.Success())
    return {};
  return result.stdout_data;
}

// ---------------------------------------------------------------------------

SystemInfo::OsInfo SystemInfo::GetOsInfo() {
  lock_guard<mutex> lk(impl_->os_mutex);
  if (!impl_->os_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->os_cache.value;
  impl_->os_cache.Set(FetchOsInfo());
  return impl_->os_cache.value;
}

// ---------------------------------------------------------------------------

SystemInfo::OsInfo SystemInfo::FetchOsInfo() {
  OsInfo info;

  // uname(2) provides kernel details and hostname.
  struct utsname u;
  if (uname(&u) == 0) {
    info.hostname = u.nodename;
    info.kernel_version = u.version;
    info.kernel_release = u.release;
    info.architecture = u.machine;
  }

  // Parse /etc/os-release for distribution details.
  string os_release = ReadFile("/etc/os-release");
  if (os_release.empty())
    os_release = ReadFile("/usr/lib/os-release");

  auto strip_quotes = [](string s) -> string {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
      return s.substr(1, s.size() - 2);
    return s;
  };

  istringstream oss(os_release);
  string line;
  while (getline(oss, line)) {
    auto eq = line.find('=');
    if (eq == string::npos)
      continue;
    string key = line.substr(0, eq);
    string val = strip_quotes(line.substr(eq + 1));
    if (key == "NAME")
      info.os_name = val;
    else if (key == "VERSION")
      info.os_version = val;
    else if (key == "ID")
      info.os_id = val;
    else if (key == "ID_LIKE")
      info.os_id_like = val;
    else if (key == "PRETTY_NAME")
      info.pretty_name = val;
  }

  // Machine ID.
  string mid = ReadFile("/etc/machine-id");
  if (!mid.empty() && mid.back() == '\n')
    mid.pop_back();
  info.machine_id = mid;

  // Uptime from /proc/uptime.
  string uptime_raw = ReadFile("/proc/uptime");
  if (!uptime_raw.empty()) {
    try {
      double up_secs = stod(uptime_raw);
      info.uptime = chrono::seconds(static_cast<long long>(up_secs));
    } catch (...) {
    }
  }

  return info;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::CpuCoreStats> SystemInfo::ParseProcStat() {
  vector<CpuCoreStats> stats;
  ifstream f("/proc/stat");
  if (!f.is_open())
    return stats;

  string line;
  while (getline(f, line)) {
    if (line.rfind("cpu", 0) != 0)
      break; // cpu* lines are always first.
    istringstream ss(line);
    string label;
    ss >> label;

    CpuCoreStats c;
    if (label == "cpu") {
      c.core_id = -1;
    } else {
      try {
        c.core_id = stoi(label.substr(3));
      } catch (...) {
        continue;
      }
    }
    ss >> c.user >> c.nice >> c.system_ticks >> c.idle >> c.iowait >> c.irq >>
      c.softirq >> c.steal >> c.guest >> c.guest_nice;
    stats.push_back(c);
  }
  return stats;
}

// ---------------------------------------------------------------------------

SystemInfo::CpuInfo SystemInfo::GetCpuInfo() {
  lock_guard<mutex> lk(impl_->cpu_mutex);
  if (!impl_->cpu_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->cpu_cache.value;
  impl_->cpu_cache.Set(FetchCpuInfo());
  return impl_->cpu_cache.value;
}

// ---------------------------------------------------------------------------

SystemInfo::CpuInfo SystemInfo::GetCpuUsage(chrono::milliseconds interval) {
  auto before = ParseProcStat();
  this_thread::sleep_for(interval);
  auto after = ParseProcStat();

  lock_guard<mutex> lk(impl_->cpu_mutex);
  CpuInfo info =
    impl_->cpu_cache.valid ? impl_->cpu_cache.value : FetchCpuInfo();

  if (!before.empty() && !after.empty()) {
    info.usage_percent = after[0].UsagePercent(before[0]);
    info.per_core_usage_percent.clear();
    for (size_t i = 1; i < after.size() && i < before.size(); ++i)
      info.per_core_usage_percent.push_back(after[i].UsagePercent(before[i]));
  }
  info.core_stats = after;
  impl_->cpu_cache.Set(info);
  return info;
}

// ---------------------------------------------------------------------------

SystemInfo::CpuInfo SystemInfo::FetchCpuInfo() {
  CpuInfo info;

  // --- Parse /proc/cpuinfo ---
  {
    ifstream f("/proc/cpuinfo");
    if (f.is_open()) {
      string line;
      bool first_cpu = true;
      set<string> phys_ids;

      while (getline(f, line)) {
        auto colon = line.find(':');
        if (colon == string::npos)
          continue;
        string key = line.substr(0, colon);
        string val = (colon + 2 < line.size()) ? line.substr(colon + 2) : "";
        // Trim trailing whitespace from key.
        while (!key.empty() && isspace(static_cast<unsigned char>(key.back())))
          key.pop_back();

        if (key == "model name" && first_cpu) {
          info.model_name = val;
          first_cpu = false;
        } else if (key == "vendor_id" && info.vendor_id.empty()) {
          info.vendor_id = val;
        } else if (key == "cpu MHz" && info.current_freq_mhz == 0.0) {
          try {
            info.current_freq_mhz = stod(val);
          } catch (...) {
          }
        } else if (key == "physical id") {
          phys_ids.insert(val);
        } else if (key == "flags" && info.flags.empty()) {
          istringstream ss(val);
          string flag;
          while (ss >> flag)
            info.flags.push_back(flag);
        } else if (key == "cache size" && info.cache_l3.empty()) {
          info.cache_l3 = val;
        }
      }
      info.sockets = static_cast<int>(phys_ids.empty() ? 1 : phys_ids.size());
    }
  }

  // Count physical cores via unique (physical_id, core_id) pairs.
  {
    ifstream f("/proc/cpuinfo");
    if (f.is_open()) {
      string line;
      string cur_phys;
      set<pair<string, string>> phys_core_pairs;
      while (getline(f, line)) {
        auto colon = line.find(':');
        if (colon == string::npos)
          continue;
        string key = line.substr(0, colon);
        string val = (colon + 2 < line.size()) ? line.substr(colon + 2) : "";
        while (!key.empty() && isspace(static_cast<unsigned char>(key.back())))
          key.pop_back();
        if (key == "physical id")
          cur_phys = val;
        else if (key == "core id")
          phys_core_pairs.insert({cur_phys, val});
      }
      info.physical_cores = static_cast<int>(phys_core_pairs.size());
    }
  }

  info.logical_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
  if (info.physical_cores == 0)
    info.physical_cores = info.logical_cores;
  if (info.sockets == 0)
    info.sockets = 1;
  info.hyper_threading =
    (info.logical_cores > info.physical_cores && info.physical_cores > 0);

  // Frequency from sysfs (cpu0 representative).
  info.max_freq_mhz =
    static_cast<double>(
      ReadUint64File("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq")) /
    1000.0;
  info.base_freq_mhz =
    static_cast<double>(
      ReadUint64File("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency")) /
    1000.0;
  if (info.base_freq_mhz == 0.0) {
    info.base_freq_mhz =
      static_cast<double>(ReadUint64File(
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq")) /
      1000.0;
  }

  // Cache sizes from sysfs index nodes.
  auto read_cache = [](int cpu, int idx) -> string {
    string path = "/sys/devices/system/cpu/cpu" + to_string(cpu) +
                  "/cache/index" + to_string(idx) + "/size";
    string s = ReadFile(path);
    if (!s.empty() && s.back() == '\n')
      s.pop_back();
    return s;
  };
  info.cache_l1d = read_cache(0, 0);
  info.cache_l1i = read_cache(0, 1);
  info.cache_l2 = read_cache(0, 2);
  if (info.cache_l3.empty() || info.cache_l3 == "0 KB")
    info.cache_l3 = read_cache(0, 3);

  // Compute usage delta against the previous snapshot.
  auto snap = ParseProcStat();
  if (!snap.empty() && !impl_->prev_cpu_stats.empty()) {
    info.usage_percent = snap[0].UsagePercent(impl_->prev_cpu_stats[0]);
    info.per_core_usage_percent.clear();
    for (size_t i = 1; i < snap.size() && i < impl_->prev_cpu_stats.size(); ++i)
      info.per_core_usage_percent.push_back(
        snap[i].UsagePercent(impl_->prev_cpu_stats[i]));
  }
  // Update baseline for the next call.
  impl_->prev_cpu_stats = snap;
  impl_->prev_cpu_time = chrono::steady_clock::now();
  info.core_stats = snap;

  return info;
}

// ---------------------------------------------------------------------------

SystemInfo::MemoryInfo SystemInfo::GetMemoryInfo() {
  lock_guard<mutex> lk(impl_->mem_mutex);
  if (!impl_->mem_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->mem_cache.value;
  impl_->mem_cache.Set(FetchMemoryInfo());
  return impl_->mem_cache.value;
}

// ---------------------------------------------------------------------------

SystemInfo::MemoryInfo SystemInfo::FetchMemoryInfo() {
  MemoryInfo m;
  ifstream f("/proc/meminfo");
  if (!f.is_open())
    return m;

  // Parse "<key>: <value> kB" lines, convert to bytes.
  auto parse_kb = [](const string &s) -> uint64_t {
    istringstream ss(s);
    uint64_t v = 0;
    ss >> v;
    return v * 1024ULL;
  };

  string line;
  while (getline(f, line)) {
    auto colon = line.find(':');
    if (colon == string::npos)
      continue;
    string key = line.substr(0, colon);
    string val = line.substr(colon + 1);
    // Trim leading whitespace from value.
    auto first = val.find_first_not_of(" \t");
    if (first != string::npos)
      val = val.substr(first);

    if (key == "MemTotal")
      m.total = parse_kb(val);
    else if (key == "MemFree")
      m.free = parse_kb(val);
    else if (key == "MemAvailable")
      m.available = parse_kb(val);
    else if (key == "Buffers")
      m.buffers = parse_kb(val);
    else if (key == "Cached")
      m.cached = parse_kb(val);
    else if (key == "SwapCached")
      m.swap_cached = parse_kb(val);
    else if (key == "Active")
      m.active = parse_kb(val);
    else if (key == "Inactive")
      m.inactive = parse_kb(val);
    else if (key == "SwapTotal")
      m.swap_total = parse_kb(val);
    else if (key == "SwapFree")
      m.swap_free = parse_kb(val);
    else if (key == "Shmem")
      m.shmem = parse_kb(val);
    else if (key == "Slab")
      m.slab = parse_kb(val);
    else if (key == "PageTables")
      m.page_tables = parse_kb(val);
    else if (key == "VmallocTotal")
      m.vmalloc_total = parse_kb(val);
    else if (key == "VmallocUsed")
      m.vmalloc_used = parse_kb(val);
    else if (key == "Hugepagesize")
      m.huge_page_size = parse_kb(val);
    else if (key == "HugePages_Total") {
      istringstream ss(val);
      ss >> m.huge_pages_total;
    } else if (key == "HugePages_Free") {
      istringstream ss(val);
      ss >> m.huge_pages_free;
    }
  }
  return m;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::DiskPartition> SystemInfo::GetDiskInfo() {
  lock_guard<mutex> lk(impl_->disk_mutex);
  if (!impl_->disk_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->disk_cache.value;
  impl_->disk_cache.Set(FetchDiskInfo());
  return impl_->disk_cache.value;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::DiskPartition> SystemInfo::FetchDiskInfo() {
  vector<DiskPartition> partitions;

  // Build an O(1) lookup set of ignored fs types.
  set<string> ignored(impl_->opts.ignored_fs_types.begin(),
                      impl_->opts.ignored_fs_types.end());

  // Use a set to avoid duplicate mount points (bind mounts etc.).
  set<string> seen_mounts;

  ifstream f("/proc/mounts");
  if (!f.is_open())
    return partitions;

  string line;
  while (getline(f, line)) {
    istringstream ss(line);
    DiskPartition p;
    ss >> p.device >> p.mount_point >> p.fs_type >> p.opts;
    if (p.mount_point.empty())
      continue;
    if (!impl_->opts.include_pseudo_filesystems && ignored.count(p.fs_type) > 0)
      continue;
    if (!seen_mounts.insert(p.mount_point).second)
      continue;

    struct statvfs sv;
    if (statvfs(p.mount_point.c_str(), &sv) != 0)
      continue;

    p.total = static_cast<uint64_t>(sv.f_blocks) * sv.f_frsize;
    p.free = static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize;
    p.used =
      (static_cast<uint64_t>(sv.f_blocks) - static_cast<uint64_t>(sv.f_bfree)) *
      sv.f_frsize;
    p.inodes_total = sv.f_files;
    p.inodes_free = sv.f_favail;
    p.inodes_used = (sv.f_files > sv.f_ffree) ? sv.f_files - sv.f_ffree : 0;

    partitions.push_back(move(p));
  }
  return partitions;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::DiskIoStats> SystemInfo::GetDiskIoStats() {
  lock_guard<mutex> lk(impl_->disk_io_mutex);
  if (!impl_->disk_io_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->disk_io_cache.value;
  impl_->disk_io_cache.Set(FetchDiskIoStats());
  return impl_->disk_io_cache.value;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::DiskIoStats> SystemInfo::FetchDiskIoStats() {
  vector<DiskIoStats> result;
  ifstream f("/proc/diskstats");
  if (!f.is_open())
    return result;

  string line;
  while (getline(f, line)) {
    istringstream ss(line);
    int major_num, minor_num;
    DiskIoStats s;
    ss >> major_num >> minor_num >> s.device >> s.reads_completed >>
      s.reads_merged >> s.sectors_read >> s.read_time_ms >>
      s.writes_completed >> s.writes_merged >> s.sectors_written >>
      s.write_time_ms >> s.io_in_progress >> s.io_time_ms >>
      s.weighted_io_time_ms;
    if (!s.device.empty())
      result.push_back(move(s));
  }
  return result;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::NetworkInterface> SystemInfo::GetNetworkInfo() {
  lock_guard<mutex> lk(impl_->net_mutex);
  if (!impl_->net_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->net_cache.value;
  impl_->net_cache.Set(FetchNetworkInfo());
  return impl_->net_cache.value;
}

// ---------------------------------------------------------------------------

void SystemInfo::ParseProcNetDev(vector<NetworkInterface> &ifaces) {
  ifstream f("/proc/net/dev");
  if (!f.is_open())
    return;

  // Build name-to-index map.
  unordered_map<string, size_t> idx;
  for (size_t i = 0; i < ifaces.size(); ++i)
    idx[ifaces[i].name] = i;

  string line;
  getline(f, line); // Skip header line 1.
  getline(f, line); // Skip header line 2.

  while (getline(f, line)) {
    auto colon = line.find(':');
    if (colon == string::npos)
      continue;
    string name = line.substr(0, colon);
    // Trim leading spaces.
    auto first_ns = name.find_first_not_of(' ');
    if (first_ns != string::npos)
      name = name.substr(first_ns);

    auto it = idx.find(name);
    if (it == idx.end()) {
      ifaces.push_back(NetworkInterface{});
      ifaces.back().name = name;
      idx[name] = ifaces.size() - 1;
      it = idx.find(name);
    }
    NetworkInterface &ni = ifaces[it->second];
    istringstream ss(line.substr(colon + 1));
    uint64_t dummy = 0;
    ss >> ni.rx_bytes >> ni.rx_packets >> ni.rx_errors >> ni.rx_dropped >>
      dummy >> dummy >> dummy >> dummy // fifo, frame, compressed, multicast
      >> ni.tx_bytes >> ni.tx_packets >> ni.tx_errors >> ni.tx_dropped;
  }
}

// ---------------------------------------------------------------------------

void SystemInfo::PopulateIfAddrs(vector<NetworkInterface> &ifaces) {
  struct ifaddrs *ifa_list = nullptr;
  if (getifaddrs(&ifa_list) != 0)
    return;

  unordered_map<string, size_t> idx;
  for (size_t i = 0; i < ifaces.size(); ++i)
    idx[ifaces[i].name] = i;

  for (struct ifaddrs *ifa = ifa_list; ifa != nullptr; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || !ifa->ifa_name)
      continue;
    string name = ifa->ifa_name;

    auto it = idx.find(name);
    if (it == idx.end()) {
      ifaces.push_back(NetworkInterface{});
      ifaces.back().name = name;
      idx[name] = ifaces.size() - 1;
      it = idx.find(name);
    }
    NetworkInterface &ni = ifaces[it->second];
    ni.is_up = (ifa->ifa_flags & IFF_UP) != 0;
    ni.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

    char buf[INET6_ADDRSTRLEN] = {};
    int family = ifa->ifa_addr->sa_family;
    if (family == AF_INET) {
      auto *sa = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
      inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
      // Avoid duplicate entries from alias interfaces.
      string addr(buf);
      if (find(ni.ip4_addresses.begin(), ni.ip4_addresses.end(), addr) ==
          ni.ip4_addresses.end())
        ni.ip4_addresses.push_back(addr);
    } else if (family == AF_INET6) {
      auto *sa = reinterpret_cast<struct sockaddr_in6 *>(ifa->ifa_addr);
      inet_ntop(AF_INET6, &sa->sin6_addr, buf, sizeof(buf));
      string addr(buf);
      if (find(ni.ip6_addresses.begin(), ni.ip6_addresses.end(), addr) ==
          ni.ip6_addresses.end())
        ni.ip6_addresses.push_back(addr);
    }
  }
  freeifaddrs(ifa_list);
}

// ---------------------------------------------------------------------------

vector<SystemInfo::NetworkInterface> SystemInfo::FetchNetworkInfo() {
  vector<NetworkInterface> ifaces;

  // /proc/net/dev gives counters and device names.
  ParseProcNetDev(ifaces);
  // getifaddrs overlay adds IP addresses and flags.
  PopulateIfAddrs(ifaces);

  // Enrich with sysfs metadata (MAC, speed, MTU).
  for (auto &ni : ifaces) {
    string base = "/sys/class/net/" + ni.name;

    string mac = ReadFile(base + "/address");
    if (!mac.empty() && mac.back() == '\n')
      mac.pop_back();
    ni.mac_address = mac;

    ni.speed_mbps = ReadUint64File(base + "/speed");
    ni.mtu = ReadUint64File(base + "/mtu");
  }
  return ifaces;
}

// ---------------------------------------------------------------------------

SystemInfo::LoadAverage SystemInfo::GetLoadAverage() {
  lock_guard<mutex> lk(impl_->load_mutex);
  if (!impl_->load_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->load_cache.value;
  impl_->load_cache.Set(FetchLoadAverage());
  return impl_->load_cache.value;
}

// ---------------------------------------------------------------------------

SystemInfo::LoadAverage SystemInfo::FetchLoadAverage() {
  LoadAverage la;
  string raw = ReadFile("/proc/loadavg");
  if (raw.empty())
    return la;
  // Format: <1min> <5min> <15min> <running>/<total> <last_pid>
  char running_total[32] = {};
  int last_pid_val = 0;
  // NOLINTNEXTLINE(cert-err34-c)
  sscanf(raw.c_str(), "%lf %lf %lf %31s %d", &la.one, &la.five, &la.fifteen,
         running_total, &last_pid_val);
  la.last_pid = static_cast<pid_t>(last_pid_val);
  // NOLINTNEXTLINE(cert-err34-c)
  sscanf(running_total, "%d/%d", &la.running_tasks, &la.total_tasks);
  return la;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ThermalZone> SystemInfo::GetThermalInfo() {
  lock_guard<mutex> lk(impl_->thermal_mutex);
  if (!impl_->thermal_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->thermal_cache.value;
  impl_->thermal_cache.Set(FetchThermalInfo());
  return impl_->thermal_cache.value;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ThermalZone> SystemInfo::FetchThermalInfo() {
  vector<ThermalZone> zones;
  const string base = "/sys/class/thermal/";
  DIR *dir = opendir(base.c_str());
  if (!dir)
    return zones;

  struct dirent *ent;
  while ((ent = readdir(dir)) != nullptr) {
    string dname = ent->d_name;
    if (dname.rfind("thermal_zone", 0) != 0)
      continue;

    ThermalZone tz;
    tz.name = dname;
    string type = ReadFile(base + dname + "/type");
    if (!type.empty() && type.back() == '\n')
      type.pop_back();
    tz.type = type;

    uint64_t milli_c = ReadUint64File(base + dname + "/temp");
    tz.temp_celsius = static_cast<double>(milli_c) / 1000.0;
    zones.push_back(tz);
  }
  closedir(dir);

  sort(
    zones.begin(), zones.end(),
    [](const ThermalZone &a, const ThermalZone &b) { return a.name < b.name; });
  return zones;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::GpuInfo> SystemInfo::GetGpuInfo() {
  lock_guard<mutex> lk(impl_->gpu_mutex);
  if (!impl_->gpu_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->gpu_cache.value;
  impl_->gpu_cache.Set(FetchGpuInfo());
  return impl_->gpu_cache.value;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::GpuInfo> SystemInfo::FetchGpuInfo() {
  vector<GpuInfo> gpus;
  if (!ExecutableExists("nvidia-smi"))
    return gpus;

  const string query =
    "--query-gpu=index,name,driver_version,memory.total,memory.used,"
    "memory.free,utilization.gpu,utilization.memory,temperature.gpu,"
    "power.draw,power.limit,pci.bus_id,compute_mode";
  string out =
    RunCommand({"nvidia-smi", query, "--format=csv,noheader,nounits"});
  if (out.empty())
    return gpus;

  istringstream ss(out);
  string line;
  while (getline(ss, line)) {
    if (line.empty())
      continue;
    GpuInfo g;
    istringstream ls(line);
    string tok;

    auto next_tok = [&]() -> string {
      if (!getline(ls, tok, ','))
        return {};
      auto s = tok.find_first_not_of(' ');
      auto e = tok.find_last_not_of(" \r\n");
      if (s == string::npos)
        return {};
      return tok.substr(s, e - s + 1);
    };

    auto safe_mb_to_bytes = [](const string &s) -> uint64_t {
      try {
        return stoull(s) * 1024ULL * 1024ULL;
      } catch (...) {
        return 0;
      }
    };
    auto safe_dbl = [](const string &s) -> double {
      try {
        return stod(s);
      } catch (...) {
        return 0.0;
      }
    };

    g.index = next_tok();
    g.name = next_tok();
    g.driver_version = next_tok();
    g.memory_total = safe_mb_to_bytes(next_tok());
    g.memory_used = safe_mb_to_bytes(next_tok());
    g.memory_free = safe_mb_to_bytes(next_tok());
    g.gpu_utilization = safe_dbl(next_tok());
    g.memory_utilization = safe_dbl(next_tok());
    g.temperature_celsius = safe_dbl(next_tok());
    g.power_draw_watts = safe_dbl(next_tok());
    g.power_limit_watts = safe_dbl(next_tok());
    g.pci_bus_id = next_tok();
    g.compute_mode = next_tok();

    gpus.push_back(move(g));
  }
  return gpus;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::DockerContainer> SystemInfo::GetDockerContainers() {
  lock_guard<mutex> lk(impl_->docker_mutex);
  if (!impl_->docker_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->docker_cache.value;
  impl_->docker_cache.Set(FetchDockerContainers());
  return impl_->docker_cache.value;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::DockerContainer> SystemInfo::FetchDockerContainers() {
  vector<DockerContainer> containers;
  if (!ExecutableExists("docker"))
    return containers;

  const string fmt =
    "{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.Status}}\t{{.State}}\t"
    "{{.CreatedAt}}";
  string out = RunCommand({"docker", "ps", "-a", "--format", fmt});
  if (out.empty())
    return containers;

  istringstream ss(out);
  string line;
  while (getline(ss, line)) {
    if (line.empty())
      continue;
    istringstream ls(line);
    DockerContainer c;
    getline(ls, c.id, '\t');
    getline(ls, c.name, '\t');
    getline(ls, c.image, '\t');
    getline(ls, c.status, '\t');
    getline(ls, c.state, '\t');
    getline(ls, c.created, '\t');
    containers.push_back(move(c));
  }
  return containers;
}

// ---------------------------------------------------------------------------

SystemInfo::SystemStats SystemInfo::GetSystemStats() {
  SystemStats s;
  s.os = GetOsInfo();
  s.cpu = GetCpuInfo();
  s.memory = GetMemoryInfo();
  s.load = GetLoadAverage();
  s.partitions = GetDiskInfo();
  s.interfaces = GetNetworkInfo();
  s.thermal_zones = GetThermalInfo();
  s.snapshot_time = chrono::system_clock::now();
  return s;
}

// ---------------------------------------------------------------------------

optional<SystemInfo::ProcessInfo>
SystemInfo::ParseProcess(pid_t pid, const string &proc_root) {
  string base = proc_root + "/" + to_string(pid);

  string stat_raw = ReadFile(base + "/stat");
  if (stat_raw.empty())
    return nullopt;

  // Comm field is enclosed in parentheses and may contain spaces.
  auto paren_open = stat_raw.find('(');
  auto paren_close = stat_raw.rfind(')');
  if (paren_open == string::npos || paren_close == string::npos ||
      paren_open >= paren_close)
    return nullopt;

  ProcessInfo p;
  p.pid = pid;
  p.name = stat_raw.substr(paren_open + 1, paren_close - paren_open - 1);

  // Fields after ')' are space-separated.
  istringstream ss(stat_raw.substr(paren_close + 2));
  char state_char = 'U';
  int ppid_val = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
  unsigned int flags_val = 0;
  unsigned long minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0;
  long utime = 0, stime = 0, cutime = 0, cstime = 0;
  long priority = 0, nice_val = 0, num_threads_val = 0, itrealvalue = 0;
  long long starttime = 0;
  unsigned long vsize = 0;
  long rss = 0;

  ss >> state_char >> ppid_val >> pgrp >> session >> tty_nr >> tpgid >>
    flags_val >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime >>
    cutime >> cstime >> priority >> nice_val >> num_threads_val >>
    itrealvalue >> starttime >> vsize >> rss;

  p.state = string(1, state_char);
  p.ppid = static_cast<pid_t>(ppid_val);
  p.num_threads = static_cast<uint64_t>(num_threads_val);
  p.vms_bytes = static_cast<uint64_t>(vsize);

  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0)
    page_size = 4096;
  p.rss_bytes = static_cast<uint64_t>(rss) * static_cast<uint64_t>(page_size);

  long ticks_per_sec = sysconf(_SC_CLK_TCK);
  if (ticks_per_sec <= 0)
    ticks_per_sec = 100;
  long cpu_ticks = utime + stime;
  p.cpu_time = chrono::seconds(cpu_ticks / ticks_per_sec);

  // Derive absolute start time from boot time + starttime ticks.
  {
    string uptime_raw = ReadFile("/proc/uptime");
    if (!uptime_raw.empty()) {
      try {
        double up_secs = stod(uptime_raw);
        auto now_tp = chrono::system_clock::now();
        auto boot_tp =
          now_tp - chrono::duration_cast<chrono::system_clock::duration>(
                     chrono::duration<double>(up_secs));
        double proc_offset =
          static_cast<double>(starttime) / static_cast<double>(ticks_per_sec);
        p.start_time =
          boot_tp + chrono::duration_cast<chrono::system_clock::duration>(
                      chrono::duration<double>(proc_offset));
      } catch (...) {
      }
    }
  }

  // Full command line (NUL bytes replaced with spaces).
  string cmdline_raw = ReadFile(base + "/cmdline");
  for (char &c : cmdline_raw)
    if (c == '\0')
      c = ' ';
  if (!cmdline_raw.empty() && cmdline_raw.back() == ' ')
    cmdline_raw.pop_back();
  p.cmdline = cmdline_raw;

  // Resolve owner uid and username.
  struct stat st;
  if (stat(base.c_str(), &st) == 0) {
    p.uid = st.st_uid;
    struct passwd pw_buf;
    struct passwd *pw_result = nullptr;
    char pw_str[1024];
    if (getpwuid_r(p.uid, &pw_buf, pw_str, sizeof(pw_str), &pw_result) == 0 &&
        pw_result != nullptr)
      p.username = pw_result->pw_name;
  }

  return p;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ProcessInfo> SystemInfo::FetchAllProcesses() {
  vector<ProcessInfo> procs;
  const string proc_root = "/proc";
  DIR *dir = opendir(proc_root.c_str());
  if (!dir)
    return procs;

  struct dirent *ent;
  while ((ent = readdir(dir)) != nullptr) {
    const char *d = ent->d_name;
    if (!d || !isdigit(static_cast<unsigned char>(d[0])))
      continue;
    pid_t pid = static_cast<pid_t>(stol(d));
    auto proc = ParseProcess(pid, proc_root);
    if (proc.has_value())
      procs.push_back(move(*proc));
  }
  closedir(dir);
  return procs;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ProcessInfo> SystemInfo::GetAllProcesses() {
  lock_guard<mutex> lk(impl_->proc_mutex);
  if (!impl_->proc_cache.IsStale(impl_->opts.cache_ttl))
    return impl_->proc_cache.value;
  impl_->proc_cache.Set(FetchAllProcesses());
  return impl_->proc_cache.value;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ProcessInfo> SystemInfo::GetTopProcessesByCpu(size_t n) {
  auto procs = GetAllProcesses();
  sort(procs.begin(), procs.end(),
       [](const ProcessInfo &a, const ProcessInfo &b) {
         if (a.cpu_percent != b.cpu_percent)
           return a.cpu_percent > b.cpu_percent;
         return a.cpu_time > b.cpu_time;
       });
  size_t limit = (n == 0) ? impl_->opts.top_process_limit : n;
  if (procs.size() > limit)
    procs.resize(limit);
  return procs;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ProcessInfo> SystemInfo::GetTopProcessesByMemory(size_t n) {
  auto procs = GetAllProcesses();
  sort(procs.begin(), procs.end(),
       [](const ProcessInfo &a, const ProcessInfo &b) {
         return a.rss_bytes > b.rss_bytes;
       });
  size_t limit = (n == 0) ? impl_->opts.top_process_limit : n;
  if (procs.size() > limit)
    procs.resize(limit);
  return procs;
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ProcessInfo>
SystemInfo::FindProcesses(const string &pattern) {
  auto procs = GetAllProcesses();
  vector<ProcessInfo> result;
  for (auto &p : procs) {
    if (p.name.find(pattern) != string::npos ||
        p.cmdline.find(pattern) != string::npos)
      result.push_back(p);
  }
  return result;
}

// ---------------------------------------------------------------------------

optional<SystemInfo::ProcessInfo> SystemInfo::GetProcess(pid_t pid) {
  return ParseProcess(pid, "/proc");
}

// ---------------------------------------------------------------------------

string SystemInfo::GetPrimaryIpv4() {
  // Find the interface associated with the default gateway route.
  ifstream f("/proc/net/route");
  if (!f.is_open())
    return {};

  string line;
  getline(f, line); // Skip header.

  string default_iface;
  while (getline(f, line)) {
    istringstream ss(line);
    string iface;
    uint32_t dest = 0, gateway = 0;
    unsigned int flags = 0;
    ss >> iface >> hex >> dest >> gateway >> flags;
    if (dest == 0) { // Default route (0.0.0.0).
      default_iface = iface;
      break;
    }
  }
  if (default_iface.empty())
    return {};

  auto ifaces = GetNetworkInfo();
  for (const auto &ni : ifaces) {
    if (ni.name == default_iface && !ni.ip4_addresses.empty())
      return ni.ip4_addresses.front();
  }
  return {};
}

// ---------------------------------------------------------------------------

string SystemInfo::GetPrimaryIpv6() {
  auto ifaces = GetNetworkInfo();
  for (const auto &ni : ifaces) {
    if (ni.is_loopback || !ni.is_up)
      continue;
    for (const auto &addr : ni.ip6_addresses) {
      // Skip link-local (fe80::/10).
      if (addr.rfind("fe80", 0) != 0)
        return addr;
    }
  }
  return {};
}

// ---------------------------------------------------------------------------

vector<SystemInfo::ListeningPort> SystemInfo::GetListeningPorts() {
  vector<ListeningPort> ports;

  // Step 1: build inode -> pid map by scanning /proc/<pid>/fd symlinks.
  unordered_map<uint64_t, pid_t> inode_to_pid;
  {
    DIR *proc_dir = opendir("/proc");
    if (proc_dir) {
      struct dirent *ent;
      while ((ent = readdir(proc_dir)) != nullptr) {
        const char *d = ent->d_name;
        if (!d || !isdigit(static_cast<unsigned char>(d[0])))
          continue;
        pid_t pid = static_cast<pid_t>(stol(d));
        string fd_dir = "/proc/" + string(d) + "/fd";
        DIR *fdir = opendir(fd_dir.c_str());
        if (!fdir)
          continue;
        struct dirent *fent;
        while ((fent = readdir(fdir)) != nullptr) {
          string fdpath = fd_dir + "/" + fent->d_name;
          char link_buf[256] = {};
          ssize_t len =
            readlink(fdpath.c_str(), link_buf, sizeof(link_buf) - 1);
          if (len <= 0)
            continue;
          string_view lv(link_buf, static_cast<size_t>(len));
          // Socket symlinks look like "socket:[<inode>]".
          if (lv.substr(0, 8) == "socket:[" && lv.back() == ']') {
            try {
              uint64_t inode = stoull(string(lv.substr(8, lv.size() - 9)));
              inode_to_pid[inode] = pid;
            } catch (...) {
            }
          }
        }
        closedir(fdir);
      }
      closedir(proc_dir);
    }
  }

  // Step 2: parse /proc/net/tcp and /proc/net/tcp6.
  auto parse_net_file = [&](const string &path, const string &proto) {
    ifstream f(path);
    if (!f.is_open())
      return;
    string line;
    getline(f, line); // Skip header.
    while (getline(f, line)) {
      istringstream ss(line);
      int sl = 0;
      string local_addr, rem_addr;
      unsigned int st = 0;
      // Format: sl local_address rem_address st tx_queue:rx_queue
      //         tr:tm->when retrnsmt uid timeout inode
      ss >> sl >> local_addr >> rem_addr >> hex >> st;
      if (st != 0x0A)
        continue; // 0x0A = TCP_LISTEN.

      // Skip tx_queue:rx_queue, tr:tm->when, retrnsmt.
      string skip;
      ss >> skip >> skip >> skip;
      int uid_val = 0, timeout_val = 0;
      uint64_t inode_val = 0;
      ss >> dec >> uid_val >> timeout_val >> inode_val;

      // Parse port from local_address field "hex_ip:hex_port".
      auto colon_pos = local_addr.find(':');
      if (colon_pos == string::npos)
        continue;
      int port_val = 0;
      try {
        port_val = static_cast<int>(
          stoul(local_addr.substr(colon_pos + 1), nullptr, 16));
      } catch (...) {
        continue;
      }

      ListeningPort lp;
      lp.port = port_val;
      lp.proto = proto;
      lp.state = "LISTEN";

      auto pid_it = inode_to_pid.find(inode_val);
      if (pid_it != inode_to_pid.end()) {
        lp.pid = pid_it->second;
        string comm = ReadFile("/proc/" + to_string(lp.pid) + "/comm");
        if (!comm.empty() && comm.back() == '\n')
          comm.pop_back();
        lp.process_name = comm;
      }
      ports.push_back(lp);
    }
  };

  parse_net_file("/proc/net/tcp", "tcp");
  parse_net_file("/proc/net/tcp6", "tcp6");

  sort(ports.begin(), ports.end(),
       [](const ListeningPort &a, const ListeningPort &b) {
         return a.port < b.port;
       });
  return ports;
}

// ---------------------------------------------------------------------------

vector<string> SystemInfo::GetDnsServers() {
  vector<string> servers;
  ifstream f("/etc/resolv.conf");
  if (!f.is_open())
    return servers;
  string line;
  while (getline(f, line)) {
    if (line.rfind("nameserver ", 0) != 0)
      continue;
    istringstream ss(line);
    string kw, addr;
    ss >> kw >> addr;
    if (!addr.empty())
      servers.push_back(addr);
  }
  return servers;
}

// ---------------------------------------------------------------------------

int SystemInfo::GetLogicalCoreCount() {
  return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}

// ---------------------------------------------------------------------------

uint64_t SystemInfo::GetTotalMemoryBytes() { return GetMemoryInfo().total; }

// ---------------------------------------------------------------------------

uint64_t SystemInfo::GetAvailableMemoryBytes() {
  return GetMemoryInfo().available;
}

// ---------------------------------------------------------------------------

chrono::seconds SystemInfo::GetUptime() {
  string raw = ReadFile("/proc/uptime");
  if (raw.empty())
    return chrono::seconds(0);
  try {
    double secs = stod(raw);
    return chrono::seconds(static_cast<long long>(secs));
  } catch (...) {
    return chrono::seconds(0);
  }
}

// ---------------------------------------------------------------------------

chrono::system_clock::time_point SystemInfo::GetBootTime() {
  auto uptime = GetUptime();
  return chrono::system_clock::now() -
         chrono::duration_cast<chrono::system_clock::duration>(uptime);
}

// ---------------------------------------------------------------------------

string SystemInfo::GetCurrentTimeString() {
  auto now = chrono::system_clock::now();
  time_t t = chrono::system_clock::to_time_t(now);
  char buf[64] = {};
  struct tm tm_buf;
  gmtime_r(&t, &tm_buf);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
  return buf;
}

// ---------------------------------------------------------------------------

string SystemInfo::GetTimezone() {
  // Debian/Ubuntu provide /etc/timezone.
  string tz = ReadFile("/etc/timezone");
  if (!tz.empty()) {
    if (tz.back() == '\n')
      tz.pop_back();
    return tz;
  }
  // Fall back to readlink /etc/localtime.
  char link_buf[256] = {};
  ssize_t len = readlink("/etc/localtime", link_buf, sizeof(link_buf) - 1);
  if (len > 0) {
    string link(link_buf, static_cast<size_t>(len));
    const string prefix = "zoneinfo/";
    auto pos = link.find(prefix);
    if (pos != string::npos)
      return link.substr(pos + prefix.size());
  }
  return "UTC";
}

// ---------------------------------------------------------------------------

int SystemInfo::GetLoggedInUserCount() {
  int count = 0;
  setutent();
  struct utmp *entry;
  while ((entry = getutent()) != nullptr) {
    if (entry->ut_type == USER_PROCESS)
      ++count;
  }
  endutent();
  return count;
}

// ---------------------------------------------------------------------------

unordered_map<string, string> SystemInfo::GetEnvironment() {
  unordered_map<string, string> env;

  for (char **e = ::environ; e && *e; ++e) {
    string kv(*e);
    auto eq = kv.find('=');
    if (eq == string::npos)
      continue;
    env[kv.substr(0, eq)] = kv.substr(eq + 1);
  }
  return env;
}

// ---------------------------------------------------------------------------

string SystemInfo::GetKernelCmdline() {
  string s = ReadFile("/proc/cmdline");
  if (!s.empty() && s.back() == '\n')
    s.pop_back();
  return s;
}

// ---------------------------------------------------------------------------

vector<string> SystemInfo::GetLoadedKernelModules() {
  vector<string> modules;
  ifstream f("/proc/modules");
  if (!f.is_open())
    return modules;
  string line;
  while (getline(f, line)) {
    istringstream ss(line);
    string name;
    ss >> name;
    if (!name.empty())
      modules.push_back(name);
  }
  return modules;
}

// ---------------------------------------------------------------------------

string SystemInfo::GetProcVersion() {
  string v = ReadFile("/proc/version");
  if (!v.empty() && v.back() == '\n')
    v.pop_back();
  return v;
}

// ---------------------------------------------------------------------------

unordered_map<string, uint64_t> SystemInfo::GetVmStats() {
  unordered_map<string, uint64_t> vm;
  ifstream f("/proc/vmstat");
  if (!f.is_open())
    return vm;
  string line;
  while (getline(f, line)) {
    istringstream ss(line);
    string key;
    uint64_t val = 0;
    if (ss >> key >> val)
      vm[key] = val;
  }
  return vm;
}

// ---------------------------------------------------------------------------

string SystemInfo::GetSummaryLine() {
  auto mem = GetMemoryInfo();
  auto la = GetLoadAverage();
  auto cpu = GetCpuInfo();

  ostringstream ss;
  ss << fixed << setprecision(1);
  ss << "CPU: " << cpu.usage_percent << "% | "
     << "Mem: " << FormatBytes(mem.Used()) << "/" << FormatBytes(mem.total)
     << " (" << FormatPercent(mem.UsedPercent()) << ") | "
     << "Load: " << la.one << "/" << la.five << "/" << la.fifteen << " | "
     << "Up: " << FormatUptime(GetUptime());
  return ss.str();
}

// ---------------------------------------------------------------------------

} // namespace system
} // namespace utils
