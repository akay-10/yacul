#include "system_info.h"

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>

using namespace utils::system;

class SystemInfoTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(SystemInfoTest, DefaultConstruction) {
  SystemInfo info;
  EXPECT_GE(info.GetLogicalCoreCount(), 1);
}

TEST_F(SystemInfoTest, OptionsConstruction) {
  SystemInfo::Options opts;
  opts.cache_ttl = std::chrono::seconds(10);
  opts.enable_background_refresh = false;
  SystemInfo info(std::move(opts));
  EXPECT_GE(info.GetLogicalCoreCount(), 1);
}

TEST_F(SystemInfoTest, GetOsInfo) {
  SystemInfo info;
  auto os = info.GetOsInfo();

  EXPECT_FALSE(os.hostname.empty());
  EXPECT_FALSE(os.kernel_release.empty());
  EXPECT_GE(os.uptime.count(), 0);
}

TEST_F(SystemInfoTest, GetCpuInfo) {
  SystemInfo info;
  auto cpu = info.GetCpuInfo();

  EXPECT_GE(cpu.logical_cores, 1);
  EXPECT_GE(cpu.physical_cores, 1);
  EXPECT_GE(cpu.sockets, 1);
}

TEST_F(SystemInfoTest, GetMemoryInfo) {
  SystemInfo info;
  auto mem = info.GetMemoryInfo();

  EXPECT_GT(mem.total, 0);
  EXPECT_GE(mem.free, 0);
  EXPECT_GE(mem.available, 0);
  EXPECT_LE(mem.available, mem.total);
}

TEST_F(SystemInfoTest, MemoryInfo_UsedPercent) {
  SystemInfo info;
  auto mem = info.GetMemoryInfo();

  double used_pct = mem.UsedPercent();
  EXPECT_GE(used_pct, 0.0);
  EXPECT_LE(used_pct, 100.0);
}

TEST_F(SystemInfoTest, GetDiskInfo) {
  SystemInfo info;
  auto disks = info.GetDiskInfo();

  EXPECT_FALSE(disks.empty());
  for (const auto &disk : disks) {
    EXPECT_FALSE(disk.device.empty());
    EXPECT_FALSE(disk.mount_point.empty());
    EXPECT_FALSE(disk.fs_type.empty());
  }
}

TEST_F(SystemInfoTest, DiskPartition_UsedPercent) {
  SystemInfo info;
  auto disks = info.GetDiskInfo();

  if (!disks.empty()) {
    double used_pct = disks[0].UsedPercent();
    EXPECT_GE(used_pct, 0.0);
    EXPECT_LE(used_pct, 100.0);
  }
}

TEST_F(SystemInfoTest, GetDiskIoStats) {
  SystemInfo info;
  auto stats = info.GetDiskIoStats();

  for (const auto &stat : stats) {
    EXPECT_FALSE(stat.device.empty());
    EXPECT_GE(stat.reads_completed, 0);
    EXPECT_GE(stat.writes_completed, 0);
  }
}

TEST_F(SystemInfoTest, GetNetworkInfo) {
  SystemInfo info;
  auto nets = info.GetNetworkInfo();

  EXPECT_FALSE(nets.empty());
  for (const auto &net : nets) {
    EXPECT_FALSE(net.name.empty());
  }
}

TEST_F(SystemInfoTest, GetLoadAverage) {
  SystemInfo info;
  auto la = info.GetLoadAverage();

  EXPECT_GE(la.one, 0.0);
  EXPECT_GE(la.five, 0.0);
  EXPECT_GE(la.fifteen, 0.0);
  EXPECT_GE(la.running_tasks, 0);
  EXPECT_GE(la.total_tasks, 0);
  EXPECT_GE(la.last_pid, 0);
}

TEST_F(SystemInfoTest, GetThermalInfo) {
  SystemInfo info;
  auto thermals = info.GetThermalInfo();

  for (const auto &tz : thermals) {
    EXPECT_FALSE(tz.name.empty());
  }
}

TEST_F(SystemInfoTest, GetGpuInfo) {
  SystemInfo info;
  auto gpus = info.GetGpuInfo();

  for (const auto &gpu : gpus) {
    EXPECT_FALSE(gpu.name.empty());
  }
}

TEST_F(SystemInfoTest, GetDockerContainers) {
  SystemInfo info;
  auto containers = info.GetDockerContainers();

  for (const auto &c : containers) {
    EXPECT_FALSE(c.id.empty());
  }
}

TEST_F(SystemInfoTest, GetSystemStats) {
  SystemInfo info;
  auto stats = info.GetSystemStats();

  EXPECT_FALSE(stats.os.hostname.empty());
  EXPECT_GE(stats.cpu.logical_cores, 1);
  EXPECT_GT(stats.memory.total, 0);
  EXPECT_GE(stats.load.one, 0.0);
  EXPECT_FALSE(stats.partitions.empty());
}

TEST_F(SystemInfoTest, GetAllProcesses) {
  SystemInfo info;
  auto procs = info.GetAllProcesses();

  EXPECT_FALSE(procs.empty());
  for (const auto &p : procs) {
    EXPECT_GT(p.pid, 0);
    EXPECT_FALSE(p.name.empty());
  }
}

TEST_F(SystemInfoTest, GetProcess_Current) {
  SystemInfo info;
  auto proc = info.GetProcess(getpid());

  ASSERT_TRUE(proc.has_value());
  EXPECT_EQ(proc->pid, getpid());
  EXPECT_FALSE(proc->name.empty());
}

TEST_F(SystemInfoTest, GetProcess_NonExistent) {
  SystemInfo info;
  auto proc = info.GetProcess(999999);

  EXPECT_FALSE(proc.has_value());
}

TEST_F(SystemInfoTest, GetTopProcessesByCpu) {
  SystemInfo info;
  auto procs = info.GetTopProcessesByCpu(5);

  EXPECT_LE(procs.size(), 5u);
}

TEST_F(SystemInfoTest, GetTopProcessesByMemory) {
  SystemInfo info;
  auto procs = info.GetTopProcessesByMemory(5);

  EXPECT_LE(procs.size(), 5u);
}

TEST_F(SystemInfoTest, FindProcesses) {
  SystemInfo info;
  auto procs = info.FindProcesses("test");

  for (const auto &p : procs) {
    bool found = p.name.find("test") != std::string::npos ||
                 p.cmdline.find("test") != std::string::npos;
    EXPECT_TRUE(found);
  }
}

TEST_F(SystemInfoTest, GetPrimaryIpv4) {
  SystemInfo info;
  std::string ip = info.GetPrimaryIpv4();
}

TEST_F(SystemInfoTest, GetPrimaryIpv6) {
  SystemInfo info;
  std::string ip = info.GetPrimaryIpv6();
}

TEST_F(SystemInfoTest, GetListeningPorts) {
  SystemInfo info;
  auto ports = info.GetListeningPorts();

  for (const auto &p : ports) {
    EXPECT_GT(p.port, 0);
    EXPECT_FALSE(p.proto.empty());
  }
}

TEST_F(SystemInfoTest, GetDnsServers) {
  SystemInfo info;
  auto servers = info.GetDnsServers();
}

TEST_F(SystemInfoTest, GetLogicalCoreCount) {
  SystemInfo info;
  int cores = info.GetLogicalCoreCount();
  EXPECT_GE(cores, 1);
}

TEST_F(SystemInfoTest, GetTotalMemoryBytes) {
  SystemInfo info;
  uint64_t total = info.GetTotalMemoryBytes();
  EXPECT_GT(total, 0);
}

TEST_F(SystemInfoTest, GetAvailableMemoryBytes) {
  SystemInfo info;
  uint64_t available = info.GetAvailableMemoryBytes();
  EXPECT_GT(available, 0);
}

TEST_F(SystemInfoTest, GetUptime) {
  SystemInfo info;
  auto uptime = info.GetUptime();
  EXPECT_GE(uptime.count(), 0);
}

TEST_F(SystemInfoTest, GetBootTime) {
  SystemInfo info;
  auto boot = info.GetBootTime();
  auto now = std::chrono::system_clock::now();
  EXPECT_LT(boot, now);
}

TEST_F(SystemInfoTest, GetCurrentTimeString) {
  SystemInfo info;
  std::string ts = info.GetCurrentTimeString();

  EXPECT_FALSE(ts.empty());
  EXPECT_EQ(ts.back(), 'Z');
  EXPECT_GE(ts.size(), 20u);
}

TEST_F(SystemInfoTest, GetTimezone) {
  SystemInfo info;
  std::string tz = info.GetTimezone();
  EXPECT_FALSE(tz.empty());
}

TEST_F(SystemInfoTest, GetLoggedInUserCount) {
  SystemInfo info;
  int count = info.GetLoggedInUserCount();
  EXPECT_GE(count, 0);
}

TEST_F(SystemInfoTest, GetEnvironment) {
  SystemInfo info;
  auto env = info.GetEnvironment();
}

TEST_F(SystemInfoTest, GetKernelCmdline) {
  SystemInfo info;
  std::string cmdline = info.GetKernelCmdline();
}

TEST_F(SystemInfoTest, GetLoadedKernelModules) {
  SystemInfo info;
  auto modules = info.GetLoadedKernelModules();
}

TEST_F(SystemInfoTest, GetProcVersion) {
  SystemInfo info;
  std::string version = info.GetProcVersion();
}

TEST_F(SystemInfoTest, GetVmStats) {
  SystemInfo info;
  auto vmstats = info.GetVmStats();
}

TEST_F(SystemInfoTest, FormatBytes) {
  EXPECT_EQ(SystemInfo::FormatBytes(0), "0 B");
  EXPECT_EQ(SystemInfo::FormatBytes(1024), "1.00 KiB");
  EXPECT_EQ(SystemInfo::FormatBytes(1024 * 1024), "1.00 MiB");
  EXPECT_EQ(SystemInfo::FormatBytes(1024ULL * 1024 * 1024), "1.00 GiB");
  EXPECT_EQ(SystemInfo::FormatBytes(1024ULL * 1024 * 1024 * 1024), "1.00 TiB");
}

TEST_F(SystemInfoTest, FormatUptime) {
  EXPECT_EQ(SystemInfo::FormatUptime(std::chrono::seconds(0)), "0s");
  EXPECT_EQ(SystemInfo::FormatUptime(std::chrono::seconds(30)), "30s");
  EXPECT_EQ(SystemInfo::FormatUptime(std::chrono::seconds(60)), "1m 0s");
  EXPECT_EQ(SystemInfo::FormatUptime(std::chrono::seconds(3661)), "1h 1m 1s");
  EXPECT_EQ(SystemInfo::FormatUptime(std::chrono::seconds(86400)), "1d 0s");
}

TEST_F(SystemInfoTest, FormatPercent) {
  EXPECT_EQ(SystemInfo::FormatPercent(0.0), "0.0%");
  EXPECT_EQ(SystemInfo::FormatPercent(50.0), "50.0%");
  EXPECT_EQ(SystemInfo::FormatPercent(100.0), "100.0%");
  EXPECT_EQ(SystemInfo::FormatPercent(33.333, 2), "33.33%");
}

TEST_F(SystemInfoTest, ReadFile) {
  std::string content = SystemInfo::ReadFile("/proc/version");
  EXPECT_FALSE(content.empty());
}

TEST_F(SystemInfoTest, ReadFile_NonExistent) {
  std::string content = SystemInfo::ReadFile("/nonexistent/file/path");
  EXPECT_TRUE(content.empty());
}

TEST_F(SystemInfoTest, ReadUint64File) {
  uint64_t val = SystemInfo::ReadUint64File("/proc/uptime", 0);
  EXPECT_GT(val, 0);
}

TEST_F(SystemInfoTest, ReadUint64File_DefaultValue) {
  uint64_t val = SystemInfo::ReadUint64File("/nonexistent/path", 42);
  EXPECT_EQ(val, 42);
}

TEST_F(SystemInfoTest, ExecutableExists) {
  EXPECT_TRUE(SystemInfo::ExecutableExists("/bin/ls"));
  EXPECT_TRUE(SystemInfo::ExecutableExists("bash"));
}

TEST_F(SystemInfoTest, ExecutableExists_NonExistent) {
  EXPECT_FALSE(SystemInfo::ExecutableExists("/nonexistent/bin/xyz"));
  EXPECT_FALSE(SystemInfo::ExecutableExists("nonexistent_command_xyz"));
}

TEST_F(SystemInfoTest, CpuCoreStats_Total) {
  SystemInfo::CpuCoreStats stats;
  stats.user = 100;
  stats.nice = 10;
  stats.system_ticks = 50;
  stats.idle = 200;
  stats.iowait = 20;
  stats.irq = 5;
  stats.softirq = 3;
  stats.steal = 2;
  stats.guest = 1;
  stats.guest_nice = 1;

  EXPECT_EQ(stats.Total(), 392u);
}

TEST_F(SystemInfoTest, CpuCoreStats_Busy) {
  SystemInfo::CpuCoreStats stats;
  stats.user = 100;
  stats.nice = 10;
  stats.system_ticks = 50;
  stats.idle = 200;
  stats.iowait = 20;
  stats.irq = 5;
  stats.softirq = 3;
  stats.steal = 2;
  stats.guest = 1;
  stats.guest_nice = 1;

  EXPECT_EQ(stats.Busy(), 170u);
}

TEST_F(SystemInfoTest, CpuCoreStats_UsagePercent) {
  SystemInfo::CpuCoreStats prev;
  prev.user = 100;
  prev.idle = 400;

  SystemInfo::CpuCoreStats curr;
  curr.user = 200;
  curr.idle = 500;

  double usage = curr.UsagePercent(prev);
  EXPECT_GE(usage, 0.0);
  EXPECT_LE(usage, 100.0);
}

TEST_F(SystemInfoTest, CpuCoreStats_UsagePercent_ZeroDelta) {
  SystemInfo::CpuCoreStats prev;
  prev.user = 100;
  prev.idle = 400;

  SystemInfo::CpuCoreStats curr;
  curr.user = 100;
  curr.idle = 400;

  double usage = curr.UsagePercent(prev);
  EXPECT_EQ(usage, 0.0);
}

TEST_F(SystemInfoTest, CpuCoreStats_UsagePercent_StaleSnapshot) {
  SystemInfo::CpuCoreStats prev;
  prev.user = 500;
  prev.idle = 400;

  SystemInfo::CpuCoreStats curr;
  curr.user = 100;
  curr.idle = 200;

  double usage = curr.UsagePercent(prev);
  EXPECT_EQ(usage, 0.0);
}

TEST_F(SystemInfoTest, InvalidateCache) {
  SystemInfo info;

  auto os1 = info.GetOsInfo();
  info.InvalidateCache();
  auto os2 = info.GetOsInfo();

  EXPECT_EQ(os1.hostname, os2.hostname);
}

TEST_F(SystemInfoTest, InvalidateCache_SpecificDomain) {
  SystemInfo info;

  auto mem1 = info.GetMemoryInfo();
  info.InvalidateCache("memory");
  auto mem2 = info.GetMemoryInfo();

  EXPECT_EQ(mem1.total, mem2.total);
}

TEST_F(SystemInfoTest, InvalidateCache_InvalidDomain) {
  SystemInfo info;

  auto os1 = info.GetOsInfo();
  info.InvalidateCache("nonexistent_domain");
  auto os2 = info.GetOsInfo();

  EXPECT_EQ(os1.hostname, os2.hostname);
}

TEST_F(SystemInfoTest, BackgroundRefresh_StartStop) {
  SystemInfo::Options opts;
  opts.enable_background_refresh = true;
  opts.refresh_interval = std::chrono::milliseconds(100);
  SystemInfo info(std::move(opts));

  EXPECT_TRUE(info.IsBackgroundRefreshRunning());

  info.StopBackgroundRefresh();
  EXPECT_FALSE(info.IsBackgroundRefreshRunning());
}

TEST_F(SystemInfoTest, BackgroundRefresh_IdempotentStart) {
  SystemInfo::Options opts;
  opts.enable_background_refresh = false;
  SystemInfo info(std::move(opts));

  info.StartBackgroundRefresh();
  EXPECT_TRUE(info.IsBackgroundRefreshRunning());

  info.StartBackgroundRefresh();
  EXPECT_TRUE(info.IsBackgroundRefreshRunning());

  info.StopBackgroundRefresh();
  EXPECT_FALSE(info.IsBackgroundRefreshRunning());
}

TEST_F(SystemInfoTest, MoveConstruction) {
  SystemInfo info1;
  auto hostname1 = info1.GetOsInfo().hostname;

  SystemInfo info2 = std::move(info1);
  auto hostname2 = info2.GetOsInfo().hostname;

  EXPECT_EQ(hostname1, hostname2);
}

TEST_F(SystemInfoTest, MoveAssignment) {
  SystemInfo info1;
  auto hostname1 = info1.GetOsInfo().hostname;

  SystemInfo info2;
  info2 = std::move(info1);
  auto hostname2 = info2.GetOsInfo().hostname;

  EXPECT_EQ(hostname1, hostname2);
}

TEST_F(SystemInfoTest, CpuUsage_BlocksForInterval) {
  SystemInfo info;

  auto t0 = std::chrono::steady_clock::now();
  auto cpu = info.GetCpuUsage(std::chrono::milliseconds(200));
  auto elapsed = std::chrono::steady_clock::now() - t0;

  EXPECT_GE(cpu.logical_cores, 1);
  EXPECT_GE(elapsed, std::chrono::milliseconds(150));
}

TEST_F(SystemInfoTest, GetSummaryLine) {
  SystemInfo info;
  std::string summary = info.GetSummaryLine();

  EXPECT_FALSE(summary.empty());
}

TEST_F(SystemInfoTest, Caching) {
  SystemInfo::Options opts;
  opts.cache_ttl = std::chrono::seconds(5);
  SystemInfo info(std::move(opts));

  auto mem1 = info.GetMemoryInfo();
  auto mem2 = info.GetMemoryInfo();

  EXPECT_EQ(mem1.total, mem2.total);
}

TEST_F(SystemInfoTest, ProcessInfo_Fields) {
  SystemInfo info;
  auto procs = info.GetAllProcesses();

  if (!procs.empty()) {
    auto &p = procs[0];
    EXPECT_GT(p.pid, 0);
    EXPECT_FALSE(p.name.empty());
    EXPECT_GE(p.num_threads, 1);
    EXPECT_GE(p.rss_bytes, 0);
    EXPECT_GE(p.vms_bytes, 0);
  }
}

TEST_F(SystemInfoTest, ProcessInfo_State) {
  SystemInfo info;
  auto proc = info.GetProcess(getpid());

  ASSERT_TRUE(proc.has_value());
  EXPECT_FALSE(proc->state.empty());
}

TEST_F(SystemInfoTest, LogSystemInfoAndStatus) {
  SystemInfo info;

  std::cout << "\n========================================\n";
  std::cout << "       SYSTEM INFORMATION & STATUS      \n";
  std::cout << "========================================\n";

  auto os = info.GetOsInfo();
  std::cout << "--- OS Information ---\n";
  std::cout << "  Hostname:        " << os.hostname << "\n";
  std::cout << "  Kernel:          " << os.kernel_release << "\n";
  std::cout << "  Architecture:   " << os.architecture << "\n";
  std::cout << "  OS Name:        " << os.os_name << "\n";
  std::cout << "  OS Version:      " << os.os_version << "\n";
  std::cout << "  Uptime:          " << SystemInfo::FormatUptime(os.uptime)
            << "\n";

  auto cpu = info.GetCpuInfo();
  std::cout << "--- CPU Information ---\n";
  std::cout << "  Model Name:      " << cpu.model_name << "\n";
  std::cout << "  Vendor:          " << cpu.vendor_id << "\n";
  std::cout << "  Physical Cores: " << cpu.physical_cores << "\n";
  std::cout << "  Logical Cores:  " << cpu.logical_cores << "\n";
  std::cout << "  Sockets:         " << cpu.sockets << "\n";
  std::cout << "  Base Freq:      " << cpu.base_freq_mhz << " MHz\n";
  std::cout << "  Max Freq:       " << cpu.max_freq_mhz << " MHz\n";
  std::cout << "  Hyper-Threading: "
            << (cpu.hyper_threading ? "Enabled" : "Disabled") << "\n";
  std::cout << "  CPU Usage:       "
            << SystemInfo::FormatPercent(cpu.usage_percent) << "\n";

  auto mem = info.GetMemoryInfo();
  std::cout << "--- Memory Information ---\n";
  std::cout << "  Total:     " << SystemInfo::FormatBytes(mem.total) << "\n";
  std::cout << "  Free:      " << SystemInfo::FormatBytes(mem.free) << "\n";
  std::cout << "  Available: " << SystemInfo::FormatBytes(mem.available)
            << "\n";
  std::cout << "  Used:      " << SystemInfo::FormatBytes(mem.Used()) << "\n";
  std::cout << "  Usage:     " << SystemInfo::FormatPercent(mem.UsedPercent())
            << "\n";
  std::cout << "  Buffers:   " << SystemInfo::FormatBytes(mem.buffers) << "\n";
  std::cout << "  Cached:    " << SystemInfo::FormatBytes(mem.cached) << "\n";
  if (mem.swap_total > 0) {
    std::cout << "  Swap Total: " << SystemInfo::FormatBytes(mem.swap_total)
              << "\n";
    std::cout << "  Swap Free:  " << SystemInfo::FormatBytes(mem.swap_free)
              << "\n";
    std::cout << "  Swap Used:  " << SystemInfo::FormatBytes(mem.SwapUsed())
              << "\n";
  }

  auto load = info.GetLoadAverage();
  std::cout << "--- Load Average ---\n";
  std::cout << "  1 min:   " << load.one << "\n";
  std::cout << "  5 min:   " << load.five << "\n";
  std::cout << "  15 min:  " << load.fifteen << "\n";
  std::cout << "  Running:  " << load.running_tasks << "/" << load.total_tasks
            << "\n";
  std::cout << "  Last PID: " << load.last_pid << "\n";

  auto disks = info.GetDiskInfo();
  std::cout << "--- Disk Partitions ---\n";
  for (const auto &disk : disks) {
    std::cout << "  " << disk.device << " (" << disk.fs_type << ")\n";
    std::cout << "    Mount: " << disk.mount_point << "\n";
    std::cout << "    Total: " << SystemInfo::FormatBytes(disk.total) << "\n";
    std::cout << "    Used:  " << SystemInfo::FormatBytes(disk.used) << " ("
              << SystemInfo::FormatPercent(disk.UsedPercent()) << ")\n";
    std::cout << "    Free:  " << SystemInfo::FormatBytes(disk.free) << "\n";
  }

  auto nets = info.GetNetworkInfo();
  std::cout << "--- Network Interfaces ---\n";
  for (const auto &net : nets) {
    std::cout << "  " << net.name << " (" << (net.is_up ? "UP" : "DOWN") << ", "
              << (net.is_loopback ? "LOOPBACK" : "physical") << ")\n";
    if (!net.mac_address.empty()) {
      std::cout << "    MAC: " << net.mac_address << "\n";
    }
    if (!net.ip4_addresses.empty()) {
      std::cout << "    IPv4: \n";
      for (const auto &ip : net.ip4_addresses) {
        std::cout << "      " << ip << "\n";
      }
    }
    if (!net.ip6_addresses.empty()) {
      std::cout << "    IPv6: \n";
      for (const auto &ip : net.ip6_addresses) {
        std::cout << "      " << ip << "\n";
      }
    }
    if (net.speed_mbps > 0) {
      std::cout << "    Speed: " << net.speed_mbps << " Mbps\n";
    }
    std::cout << "    RX: " << SystemInfo::FormatBytes(net.rx_bytes) << "\n";
    std::cout << "    TX: " << SystemInfo::FormatBytes(net.tx_bytes) << "\n";
  }

  auto thermal = info.GetThermalInfo();
  if (!thermal.empty()) {
    std::cout << "--- Thermal Zones ---\n";
    for (const auto &tz : thermal) {
      std::cout << "  " << tz.name << " (" << tz.type
                << "): " << tz.temp_celsius << " C\n";
    }
  }

  auto gpus = info.GetGpuInfo();
  if (!gpus.empty()) {
    std::cout << "--- GPU Information ---\n";
    for (const auto &gpu : gpus) {
      std::cout << "  GPU " << gpu.index << ": " << gpu.name << "\n";
      std::cout << "    Driver:     " << gpu.driver_version << "\n";
      std::cout << "    CUDA:       " << gpu.cuda_version << "\n";
      std::cout << "    Memory:     "
                << SystemInfo::FormatBytes(gpu.memory_used) << " / "
                << SystemInfo::FormatBytes(gpu.memory_total) << "\n";
      std::cout << "    GPU Util:   "
                << SystemInfo::FormatPercent(gpu.gpu_utilization) << "\n";
      std::cout << "    Mem Util:   "
                << SystemInfo::FormatPercent(gpu.memory_utilization) << "\n";
      std::cout << "    Temperature:" << gpu.temperature_celsius << " C\n";
      std::cout << "    Power:      " << gpu.power_draw_watts << " W / "
                << gpu.power_limit_watts << " W\n";
    }
  }

  auto containers = info.GetDockerContainers();
  if (!containers.empty()) {
    std::cout << "--- Docker Containers ---\n";
    for (const auto &c : containers) {
      std::cout << "  " << c.name << " (" << c.image << ")\n";
      std::cout << "    Status: " << c.status << "\n";
      std::cout << "    State:  " << c.state << "\n";
    }
  }

  auto top_cpu = info.GetTopProcessesByCpu(5);
  std::cout << "--- Top 5 Processes by CPU ---\n";
  for (size_t i = 0; i < top_cpu.size(); ++i) {
    const auto &p = top_cpu[i];
    std::cout << "  " << (i + 1) << ". " << p.name << " (PID: " << p.pid << ")"
              << " - CPU: " << SystemInfo::FormatPercent(p.cpu_percent)
              << ", Memory: " << SystemInfo::FormatBytes(p.rss_bytes) << "\n";
  }

  auto top_mem = info.GetTopProcessesByMemory(5);
  std::cout << "--- Top 5 Processes by Memory ---\n";
  for (size_t i = 0; i < top_mem.size(); ++i) {
    const auto &p = top_mem[i];
    std::cout << "  " << (i + 1) << ". " << p.name << " (PID: " << p.pid << ")"
              << " - Memory: " << SystemInfo::FormatBytes(p.rss_bytes) << "\n";
  }

  auto ports = info.GetListeningPorts();
  std::cout << "--- Listening Ports ---\n";
  std::cout << "  Total: " << ports.size() << " listening ports\n";
  for (size_t i = 0; i < std::min(ports.size(), size_t(10)); ++i) {
    const auto &p = ports[i];
    std::cout << "    " << p.proto << " " << p.port << " - " << p.process_name
              << " (PID: " << p.pid << ")\n";
  }

  auto dns = info.GetDnsServers();
  std::cout << "--- DNS Servers ---\n";
  for (const auto &server : dns) {
    std::cout << "  " << server << "\n";
  }

  std::cout << "--- System Summary ---\n";
  std::cout << "  " << info.GetSummaryLine() << "\n";

  std::cout << "========================================\n";
  std::cout << "Current Time: " << info.GetCurrentTimeString() << "\n";
  std::cout << "Timezone:     " << info.GetTimezone() << "\n";
  std::cout << "========================================\n";

  SUCCEED();
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
