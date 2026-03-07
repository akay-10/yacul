#ifndef UTILS_SYSTEM_SYSTEM_INFO_H
#define UTILS_SYSTEM_SYSTEM_INFO_H

#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "basic/basic.h"
#include "popen_wrapper.h"

namespace utils {
namespace system {

// SystemInfo provides a production-grade, thread-safe, high-performance API
// for querying and monitoring system resources on Linux. It reads from /proc,
// /sys, and via PopenWrapper-driven commands.
//
// Design goals:
//  - Lazy evaluation: data is fetched only when requested.
//  - Caching with TTL: repeated calls within the TTL window return cached data.
//  - Background refresh: optional watcher thread keeps caches warm.
//  - Zero heap allocation on hot paths where possible.
//  - All structs are value types; pointers are never leaked.
//
// Thread safety:
//  - All public methods are safe to call concurrently from multiple threads.
//  - A single SystemInfo instance may be shared freely.
//
// Note on move semantics: SystemInfo holds non-movable members (std::mutex,
// std::atomic). Move construction/assignment are implemented via internal
// Impl pimpl, keeping the public API clean.
//
// Usage example:
//   SystemInfo::Options opts;
//   opts.cache_ttl = std::chrono::seconds(5);
//   SystemInfo info(std::move(opts));
//
//   auto cpu  = info.GetCpuInfo();
//   auto mem  = info.GetMemoryInfo();
//   auto disk = info.GetDiskInfo();

class SystemInfo {
public:
  using Ptr = std::shared_ptr<SystemInfo>;
  using PtrConst = std::shared_ptr<const SystemInfo>;

  // ---------------------------------------------------------------------------
  // Public types – plain-old-data structs, all fields zero-initialised.
  // ---------------------------------------------------------------------------

  // OsInfo describes the operating system and kernel.
  struct OsInfo {
    std::string hostname;           // e.g. "myserver"
    std::string kernel_version;     // e.g. "6.1.0-21-amd64"
    std::string kernel_release;     // e.g. "6.1.72-1"
    std::string architecture;       // e.g. "x86_64"
    std::string os_name;            // e.g. "Ubuntu"
    std::string os_version;         // e.g. "22.04.3 LTS"
    std::string os_id;              // e.g. "ubuntu"
    std::string os_id_like;         // e.g. "debian"
    std::string pretty_name;        // e.g. "Ubuntu 22.04.3 LTS"
    std::string machine_id;         // /etc/machine-id
    std::chrono::seconds uptime{0}; // System uptime.
  };

  // CpuCoreStats holds per-core tick counters from /proc/stat.
  struct CpuCoreStats {
    int core_id = -1; // -1 = aggregate "cpu" line.
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system_ticks = 0; // Named system_ticks to avoid POSIX macro clash.
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;
    uint64_t guest = 0;
    uint64_t guest_nice = 0;

    // Total ticks across all states.
    uint64_t Total() const;
    // Ticks spent doing actual work (excludes idle/iowait).
    uint64_t Busy() const;
    // Usage percentage relative to a prior snapshot.
    double UsagePercent(const CpuCoreStats &prev) const;
  };

  // CpuInfo describes static CPU topology and dynamic utilisation.
  struct CpuInfo {
    std::string model_name;         // e.g. "Intel(R) Core(TM) i7-..."
    std::string vendor_id;          // e.g. "GenuineIntel"
    int physical_cores = 0;         // Physical cores (unique core ids).
    int logical_cores = 0;          // Logical cores (nproc).
    int sockets = 0;                // Socket count.
    double base_freq_mhz = 0.0;     // From base_frequency sysfs node.
    double max_freq_mhz = 0.0;      // From scaling_max_freq sysfs node.
    double current_freq_mhz = 0.0;  // From scaling_cur_freq sysfs node.
    std::string cache_l1d;          // L1 data cache size string.
    std::string cache_l1i;          // L1 instruction cache size string.
    std::string cache_l2;           // L2 cache size string.
    std::string cache_l3;           // L3 cache size string.
    std::vector<std::string> flags; // CPU feature flags.
    bool hyper_threading = false;

    // Per-core tick counters. Index 0 = aggregate; 1..N = per-core.
    std::vector<CpuCoreStats> core_stats;
    // Aggregate usage percentage since last snapshot (0-100).
    double usage_percent = 0.0;
    // Per-core usage percentages (index = logical core id).
    std::vector<double> per_core_usage_percent;
  };

  // MemoryInfo mirrors /proc/meminfo fields (all values in bytes).
  struct MemoryInfo {
    uint64_t total = 0;
    uint64_t free = 0;
    uint64_t available = 0;
    uint64_t buffers = 0;
    uint64_t cached = 0;
    uint64_t swap_cached = 0;
    uint64_t active = 0;
    uint64_t inactive = 0;
    uint64_t swap_total = 0;
    uint64_t swap_free = 0;
    uint64_t shmem = 0;
    uint64_t slab = 0;
    uint64_t page_tables = 0;
    uint64_t vmalloc_total = 0;
    uint64_t vmalloc_used = 0;
    uint64_t huge_pages_total = 0;
    uint64_t huge_pages_free = 0;
    uint64_t huge_page_size = 0;

    // Derived helpers.
    double UsedPercent() const;
    double SwapUsedPercent() const;
    uint64_t Used() const { return total > available ? total - available : 0; }
    uint64_t SwapUsed() const {
      return swap_total > swap_free ? swap_total - swap_free : 0;
    }
  };

  // DiskPartition describes a single mounted filesystem.
  struct DiskPartition {
    std::string device;      // e.g. "/dev/sda1"
    std::string mount_point; // e.g. "/"
    std::string fs_type;     // e.g. "ext4"
    std::string opts;        // Mount options string.
    uint64_t total = 0;      // Total bytes.
    uint64_t used = 0;       // Used bytes.
    uint64_t free = 0;       // Free bytes (available to non-root).
    uint64_t inodes_total = 0;
    uint64_t inodes_used = 0;
    uint64_t inodes_free = 0;
    double UsedPercent() const;
    double InodesUsedPercent() const;
  };

  // DiskIoStats holds cumulative I/O counters from /proc/diskstats.
  struct DiskIoStats {
    std::string device; // e.g. "sda"
    uint64_t reads_completed = 0;
    uint64_t reads_merged = 0;
    uint64_t sectors_read = 0;
    uint64_t read_time_ms = 0;
    uint64_t writes_completed = 0;
    uint64_t writes_merged = 0;
    uint64_t sectors_written = 0;
    uint64_t write_time_ms = 0;
    uint64_t io_in_progress = 0;
    uint64_t io_time_ms = 0;
    uint64_t weighted_io_time_ms = 0;
  };

  // NetworkInterface describes a single NIC.
  struct NetworkInterface {
    std::string name;        // e.g. "eth0"
    std::string mac_address; // e.g. "aa:bb:cc:dd:ee:ff"
    std::vector<std::string> ip4_addresses;
    std::vector<std::string> ip6_addresses;
    bool is_up = false;
    bool is_loopback = false;
    uint64_t speed_mbps = 0; // From /sys/class/net/<iface>/speed, 0=unknown.
    uint64_t mtu = 0;

    // /proc/net/dev counters.
    uint64_t rx_bytes = 0;
    uint64_t rx_packets = 0;
    uint64_t rx_errors = 0;
    uint64_t rx_dropped = 0;
    uint64_t tx_bytes = 0;
    uint64_t tx_packets = 0;
    uint64_t tx_errors = 0;
    uint64_t tx_dropped = 0;
  };

  // ThermalZone describes one kernel thermal zone.
  struct ThermalZone {
    std::string name; // e.g. "thermal_zone0"
    std::string type; // Contents of thermal_zone*/type
    double temp_celsius = 0.0;
  };

  // ProcessInfo describes a single running process.
  struct ProcessInfo {
    pid_t pid = 0;
    pid_t ppid = 0;
    std::string name;     // Comm field (15-char truncated).
    std::string cmdline;  // Full command line (NUL-joined).
    std::string state;    // R/S/D/Z/T etc.
    std::string username; // Resolved from uid.
    uid_t uid = 0;
    double cpu_percent = 0.0; // CPU since last snapshot.
    uint64_t rss_bytes = 0;   // Resident set size.
    uint64_t vms_bytes = 0;   // Virtual memory size.
    uint64_t num_threads = 0;
    std::chrono::seconds cpu_time{0}; // Total CPU time (user+system).
    std::chrono::system_clock::time_point start_time;
  };

  // LoadAverage holds the 1/5/15-minute load averages from /proc/loadavg.
  struct LoadAverage {
    double one = 0.0;
    double five = 0.0;
    double fifteen = 0.0;
    int running_tasks = 0;
    int total_tasks = 0;
    pid_t last_pid = 0;
  };

  // SystemStats is a convenience aggregate of the most-queried metrics.
  struct SystemStats {
    OsInfo os;
    CpuInfo cpu;
    MemoryInfo memory;
    LoadAverage load;
    std::vector<DiskPartition> partitions;
    std::vector<NetworkInterface> interfaces;
    std::vector<ThermalZone> thermal_zones;
    std::chrono::system_clock::time_point snapshot_time;
  };

  // GpuInfo describes a detected GPU (NVIDIA via nvidia-smi).
  struct GpuInfo {
    std::string index; // GPU index string.
    std::string name;  // e.g. "Tesla T4"
    std::string driver_version;
    std::string cuda_version;
    uint64_t memory_total = 0;       // bytes
    uint64_t memory_used = 0;        // bytes
    uint64_t memory_free = 0;        // bytes
    double gpu_utilization = 0.0;    // 0-100 %
    double memory_utilization = 0.0; // 0-100 %
    double temperature_celsius = 0.0;
    double power_draw_watts = 0.0;
    double power_limit_watts = 0.0;
    std::string pci_bus_id;
    std::string compute_mode;
  };

  // DockerContainer summarises a running container (requires Docker CLI).
  struct DockerContainer {
    std::string id;
    std::string name;
    std::string image;
    std::string status;
    std::string state;
    std::string created;
    std::vector<std::string> ports;
  };

  // Options controls caching and background-refresh behaviour.
  // Defined before the constructor so Options{} default arg is valid.
  struct Options {
    // How long a cached value is considered fresh. Zero disables caching.
    std::chrono::milliseconds cache_ttl{std::chrono::seconds(5)};

    // When true, a background thread refreshes caches proactively.
    bool enable_background_refresh = false;

    // Interval at which the background thread refreshes.
    std::chrono::milliseconds refresh_interval{std::chrono::seconds(3)};

    // Filesystem types to skip when enumerating disk partitions.
    std::vector<std::string> ignored_fs_types{
        "tmpfs",   "devtmpfs",   "squashfs", "overlay",   "proc",   "sysfs",
        "cgroup",  "cgroup2",    "devpts",   "hugetlbfs", "mqueue", "debugfs",
        "tracefs", "securityfs", "fusectl",  "pstore"};

    // If true, include pseudo-filesystems in partition results.
    bool include_pseudo_filesystems = false;

    // Maximum number of top processes to return in GetTopProcesses*().
    std::size_t top_process_limit = 20;

    // Timeout for PopenWrapper-driven commands.
    std::chrono::milliseconds command_timeout{std::chrono::seconds(10)};
  };

  // ---------------------------------------------------------------------------
  // Construction / destruction
  // ---------------------------------------------------------------------------

  // Construct with default options.
  SystemInfo();
  // Construct with custom options.
  explicit SystemInfo(Options options);

  // Not copyable – mutexes are not copyable.
  DISALLOW_COPY_AND_ASSIGN(SystemInfo);

  // Move is implemented via Impl pimpl (mutexes are not movable by default).
  SystemInfo(SystemInfo &&other) noexcept;
  SystemInfo &operator=(SystemInfo &&other) noexcept;

  ~SystemInfo();

  // ---------------------------------------------------------------------------
  // Core queries – all return by value; results are cached internally.
  // ---------------------------------------------------------------------------

  // Static OS and kernel information. Seldom changes; long TTL is safe.
  OsInfo GetOsInfo();

  // CPU topology and utilisation snapshot. For an accurate instantaneous
  // reading use GetCpuUsage() which blocks for a sampling interval.
  CpuInfo GetCpuInfo();

  // Compute CPU usage by diffing two /proc/stat snapshots separated by
  // 'interval'. Blocks the caller for 'interval'.
  CpuInfo GetCpuUsage(
      std::chrono::milliseconds interval = std::chrono::milliseconds(500));

  // Memory information from /proc/meminfo.
  MemoryInfo GetMemoryInfo();

  // Mounted disk partitions with capacity statistics.
  std::vector<DiskPartition> GetDiskInfo();

  // Raw I/O counters from /proc/diskstats.
  std::vector<DiskIoStats> GetDiskIoStats();

  // Network interface list with addresses and /proc/net/dev counters.
  std::vector<NetworkInterface> GetNetworkInfo();

  // System load averages from /proc/loadavg.
  LoadAverage GetLoadAverage();

  // Thermal zone temperatures from /sys/class/thermal/.
  std::vector<ThermalZone> GetThermalInfo();

  // GPU information via nvidia-smi (returns empty if unavailable).
  std::vector<GpuInfo> GetGpuInfo();

  // Docker container list (returns empty if Docker is not available).
  std::vector<DockerContainer> GetDockerContainers();

  // Snapshot of all commonly-needed metrics in one call.
  SystemStats GetSystemStats();

  // ---------------------------------------------------------------------------
  // Process queries
  // ---------------------------------------------------------------------------

  // Return all running processes.
  std::vector<ProcessInfo> GetAllProcesses();

  // Return the top N processes sorted by CPU usage (descending).
  std::vector<ProcessInfo> GetTopProcessesByCpu(std::size_t n = 0);

  // Return the top N processes sorted by memory (RSS) usage (descending).
  std::vector<ProcessInfo> GetTopProcessesByMemory(std::size_t n = 0);

  // Find processes whose name or cmdline contains 'pattern'.
  std::vector<ProcessInfo> FindProcesses(const std::string &pattern);

  // Return info for a specific PID, or nullopt if the process does not exist.
  std::optional<ProcessInfo> GetProcess(pid_t pid);

  // ---------------------------------------------------------------------------
  // Network helpers
  // ---------------------------------------------------------------------------

  // Return the primary outbound IPv4 address, or empty string.
  std::string GetPrimaryIpv4();

  // Return the primary outbound IPv6 address (non-link-local), or empty.
  std::string GetPrimaryIpv6();

  // Describes a single TCP/UDP listening port.
  struct ListeningPort {
    int port = 0;
    std::string proto; // "tcp" or "tcp6"
    std::string state;
    pid_t pid = -1;
    std::string process_name;
  };

  // Return listening TCP ports parsed from /proc/net/tcp[6].
  std::vector<ListeningPort> GetListeningPorts();

  // Return DNS resolver addresses from /etc/resolv.conf.
  std::vector<std::string> GetDnsServers();

  // ---------------------------------------------------------------------------
  // Convenience system-level helpers
  // ---------------------------------------------------------------------------

  // Number of online logical CPU cores.
  int GetLogicalCoreCount();

  // Total installed RAM in bytes.
  uint64_t GetTotalMemoryBytes();

  // Available memory in bytes (MemAvailable from /proc/meminfo).
  uint64_t GetAvailableMemoryBytes();

  // System uptime since last boot.
  std::chrono::seconds GetUptime();

  // Wall-clock boot time.
  std::chrono::system_clock::time_point GetBootTime();

  // Current system time as ISO-8601 string (UTC).
  std::string GetCurrentTimeString();

  // Timezone identifier, e.g. "America/New_York".
  std::string GetTimezone();

  // Number of logged-in users from utmp.
  int GetLoggedInUserCount();

  // Process environment as key/value map.
  std::unordered_map<std::string, std::string> GetEnvironment();

  // Kernel boot parameters from /proc/cmdline.
  std::string GetKernelCmdline();

  // Names of loaded kernel modules from /proc/modules.
  std::vector<std::string> GetLoadedKernelModules();

  // Raw content of /proc/version.
  std::string GetProcVersion();

  // Virtual memory statistics from /proc/vmstat.
  std::unordered_map<std::string, uint64_t> GetVmStats();

  // ---------------------------------------------------------------------------
  // Cache control
  // ---------------------------------------------------------------------------

  // Invalidate all cached data, forcing re-fetch on next access.
  void InvalidateCache();

  // Invalidate a specific cache domain. Valid keys: "os", "cpu", "memory",
  // "disk", "disk_io", "network", "thermal", "gpu", "processes",
  // "load", "docker".
  void InvalidateCache(const std::string &domain);

  // ---------------------------------------------------------------------------
  // Background refresh control
  // ---------------------------------------------------------------------------

  // Start background refresh thread (idempotent if already running).
  void StartBackgroundRefresh();

  // Stop background refresh thread and join it.
  void StopBackgroundRefresh();

  // True if the background thread is currently running.
  bool IsBackgroundRefreshRunning() const;

  // ---------------------------------------------------------------------------
  // Formatting helpers (static, no locking needed)
  // ---------------------------------------------------------------------------

  // Format bytes as a human-readable string, e.g. "3.72 GiB".
  static std::string FormatBytes(uint64_t bytes);

  // Format a duration as "Xd Xh Xm Xs".
  static std::string FormatUptime(std::chrono::seconds uptime);

  // Format a percentage with 'decimals' decimal places.
  static std::string FormatPercent(double pct, int decimals = 1);

  // Return a compact one-line summary string of current system health.
  std::string GetSummaryLine();

  // ---------------------------------------------------------------------------
  // Low-level static file helpers
  // ---------------------------------------------------------------------------

  // Read the entire content of a procfs/sysfs file. Returns "" on error.
  static std::string ReadFile(const std::string &path);

  // Read a single uint64 value from a sysfs/procfs file.
  static uint64_t ReadUint64File(const std::string &path,
                                 uint64_t default_value = 0);

  // Check whether an executable exists on PATH or at an absolute path.
  static bool ExecutableExists(const std::string &name);

private:
  // ---------------------------------------------------------------------------
  // Impl – holds all non-movable state behind a unique_ptr so that
  // SystemInfo itself can be moved cheaply and correctly.
  // ---------------------------------------------------------------------------

  struct Impl {
    Options opts;

    // Per-domain mutexes for fine-grained locking.
    mutable std::mutex os_mutex;
    mutable std::mutex cpu_mutex;
    mutable std::mutex mem_mutex;
    mutable std::mutex disk_mutex;
    mutable std::mutex disk_io_mutex;
    mutable std::mutex net_mutex;
    mutable std::mutex thermal_mutex;
    mutable std::mutex gpu_mutex;
    mutable std::mutex docker_mutex;
    mutable std::mutex proc_mutex;
    mutable std::mutex load_mutex;

    // ---------------------------------------------------------------------------
    // Generic TTL cache entry.
    // ---------------------------------------------------------------------------
    template <typename T> struct CacheEntry {
      T value{};
      std::chrono::steady_clock::time_point fetched_at{};
      bool valid = false;

      bool IsStale(std::chrono::milliseconds ttl) const {
        if (!valid || ttl.count() == 0)
          return true;
        return (std::chrono::steady_clock::now() - fetched_at) > ttl;
      }

      void Set(T v) {
        value = std::move(v);
        fetched_at = std::chrono::steady_clock::now();
        valid = true;
      }
    };

    CacheEntry<OsInfo> os_cache;
    CacheEntry<CpuInfo> cpu_cache;
    CacheEntry<MemoryInfo> mem_cache;
    CacheEntry<std::vector<DiskPartition>> disk_cache;
    CacheEntry<std::vector<DiskIoStats>> disk_io_cache;
    CacheEntry<std::vector<NetworkInterface>> net_cache;
    CacheEntry<LoadAverage> load_cache;
    CacheEntry<std::vector<ThermalZone>> thermal_cache;
    CacheEntry<std::vector<GpuInfo>> gpu_cache;
    CacheEntry<std::vector<DockerContainer>> docker_cache;
    CacheEntry<std::vector<ProcessInfo>> proc_cache;

    // Previous /proc/stat snapshot for CPU delta computation.
    std::vector<CpuCoreStats> prev_cpu_stats;
    std::chrono::steady_clock::time_point prev_cpu_time;

    // Background refresh.
    std::atomic<bool> refresh_running{false};
    std::thread refresh_thread;

    explicit Impl(Options o) : opts(std::move(o)) {}
  };

  std::unique_ptr<Impl> impl_;

  // ---------------------------------------------------------------------------
  // Internal fetch helpers (called under appropriate locks)
  // ---------------------------------------------------------------------------

  OsInfo FetchOsInfo();
  CpuInfo FetchCpuInfo();
  MemoryInfo FetchMemoryInfo();
  std::vector<DiskPartition> FetchDiskInfo();
  std::vector<DiskIoStats> FetchDiskIoStats();
  std::vector<NetworkInterface> FetchNetworkInfo();
  LoadAverage FetchLoadAverage();
  std::vector<ThermalZone> FetchThermalInfo();
  std::vector<GpuInfo> FetchGpuInfo();
  std::vector<DockerContainer> FetchDockerContainers();
  std::vector<ProcessInfo> FetchAllProcesses();

  // Parse a single process entry from /proc/<pid>.
  std::optional<ProcessInfo> ParseProcess(pid_t pid,
                                          const std::string &proc_root);

  // Parse /proc/stat into a vector of CpuCoreStats.
  std::vector<CpuCoreStats> ParseProcStat();

  // Run a command via PopenWrapper and return stdout, or "" on failure.
  std::string RunCommand(const std::vector<std::string> &argv);

  // Populate rx/tx counters from /proc/net/dev into existing interfaces.
  void ParseProcNetDev(std::vector<NetworkInterface> &ifaces);

  // Populate IP addresses via getifaddrs().
  void PopulateIfAddrs(std::vector<NetworkInterface> &ifaces);

  // Background refresh loop body.
  void RefreshLoop();
};

} // namespace system
} // namespace utils

#endif // UTILS_SYSTEM_SYSTEM_INFO_H
