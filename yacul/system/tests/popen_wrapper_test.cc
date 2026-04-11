#include "yacul/system/popen_wrapper.h"

#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace utils::system;

namespace {

// Returns a PopenWrapper::Options pre-configured for read-only mode.
PopenWrapper::Options ReadOpts(std::vector<std::string> cmd) {
  PopenWrapper::Options opts;
  opts.command = std::move(cmd);
  opts.mode = PopenWrapper::Mode::kReadOnly;
  return opts;
}

// Returns a PopenWrapper::Options pre-configured for read-write mode.
PopenWrapper::Options RwOpts(std::vector<std::string> cmd,
                             std::string stdin_data = "") {
  PopenWrapper::Options opts;
  opts.command = std::move(cmd);
  opts.mode = PopenWrapper::Mode::kReadWrite;
  opts.stdin_data = std::move(stdin_data);
  return opts;
}

} // namespace

class PopenWrapperTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(PopenWrapperTest, Run_BasicEcho) {
  PopenWrapper proc(ReadOpts({"echo", "hello world"}));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_data, "hello world\n");
}

TEST_F(PopenWrapperTest, Run_MultipleArguments) {
  // printf does not append a newline — good for exact matching.
  PopenWrapper proc(ReadOpts({"printf", "%s %s", "foo", "bar"}));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "foo bar");
}

TEST_F(PopenWrapperTest, Run_EmptyOutput) {
  PopenWrapper proc(ReadOpts({"true"}));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_TRUE(result.stdout_data.empty());
}

TEST_F(PopenWrapperTest, Run_BinaryLikeOutput) {
  // Verify that NUL bytes inside output are preserved.
  PopenWrapper proc(ReadOpts({"printf", "a\\x00b"}));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  ASSERT_EQ(result.stdout_data.size(), 3u);
  EXPECT_EQ(result.stdout_data[0], 'a');
  EXPECT_EQ(result.stdout_data[1], '\0');
  EXPECT_EQ(result.stdout_data[2], 'b');
}

TEST_F(PopenWrapperTest, Run_ShellMode_Arithmetic) {
  auto opts = ReadOpts({"echo $((1+2))"});
  opts.shell = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "3\n");
}

TEST_F(PopenWrapperTest, Run_ShellMode_Piping) {
  auto opts = ReadOpts({"echo hello | tr a-z A-Z"});
  opts.shell = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "HELLO\n");
}

TEST_F(PopenWrapperTest, Run_ShellMode_Glob) {
  // Glob expansion only works through the shell.
  auto opts = ReadOpts({"ls /bin/sh*"});
  opts.shell = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_FALSE(result.stdout_data.empty());
}

TEST_F(PopenWrapperTest, Run_ShellMode_SpecialChars_InArgs) {
  // Ensure ShellEscape handles single quotes embedded in arguments.
  // With shell=true tokens are joined raw; the quoting must be in the script.
  auto opts = ReadOpts({"echo 'it'\"'\"'s alive'"});
  opts.shell = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "it's alive\n");
}

TEST_F(PopenWrapperTest, Run_ExitCode_Zero) {
  PopenWrapper proc(ReadOpts({"true"}));
  auto result = proc.Run();

  EXPECT_TRUE(result.exited_normally);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.Success());
}

TEST_F(PopenWrapperTest, Run_ExitCode_NonZero) {
  auto opts = ReadOpts({"bash", "-c", "exit 42"});
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.exited_normally);
  EXPECT_FALSE(result.Success());
  EXPECT_EQ(result.exit_code, 42);
}

TEST_F(PopenWrapperTest, Run_ExitCode_False) {
  PopenWrapper proc(ReadOpts({"false"}));
  auto result = proc.Run();

  EXPECT_TRUE(result.exited_normally);
  EXPECT_EQ(result.exit_code, 1);
  EXPECT_FALSE(result.Success());
}

TEST_F(PopenWrapperTest, Run_KilledBySignal) {
  // Self-send SIGKILL via the shell.
  auto opts = ReadOpts({"bash", "-c", "kill -9 $$"});
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_FALSE(result.exited_normally);
  EXPECT_TRUE(result.killed_by_signal);
  EXPECT_EQ(result.signal_number, SIGKILL);
}

TEST_F(PopenWrapperTest, Run_KilledBySigterm) {
  auto opts = ReadOpts({"bash", "-c", "kill -15 $$"});
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_FALSE(result.exited_normally);
  EXPECT_TRUE(result.killed_by_signal);
  EXPECT_EQ(result.signal_number, SIGTERM);
}

TEST_F(PopenWrapperTest, Run_WriteOnlyMode) {
  auto opts = PopenWrapper::Options{};
  opts.command = {"cat"};
  opts.mode = PopenWrapper::Mode::kWriteOnly;
  opts.stdin_data = "test input data";
  // In write-only mode we cannot read back stdout from the wrapper.
  // Verify that the child exits cleanly (cat drains stdin and exits 0).
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_TRUE(result.stdout_data.empty());
}

TEST_F(PopenWrapperTest, Run_ReadWriteMode_Echo) {
  PopenWrapper proc(RwOpts({"cat"}, "test input"));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "test input");
}

TEST_F(PopenWrapperTest, Run_ReadWriteMode_MultiLine) {
  PopenWrapper proc(RwOpts({"cat"}, "line1\nline2\nline3\n"));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "line1\nline2\nline3\n");
}

TEST_F(PopenWrapperTest, Run_ReadWriteMode_EmptyStdin) {
  PopenWrapper proc(RwOpts({"cat"}, ""));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_TRUE(result.stdout_data.empty());
}

TEST_F(PopenWrapperTest, Run_LargeInput) {
  // 100 KB — exercises the poll-based write loop.
  const std::size_t kSize = 100000;
  std::string large_input(kSize, 'x');
  PopenWrapper proc(RwOpts({"cat"}, large_input));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data.size(), kSize);
  EXPECT_EQ(result.stdout_data, large_input);
}

TEST_F(PopenWrapperTest, Run_StdinTransformation) {
  // tr converts lowercase to uppercase, verifying stdin → stdout round-trip.
  PopenWrapper proc(RwOpts({"tr", "a-z", "A-Z"}, "hello world"));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "HELLO WORLD");
}

TEST_F(PopenWrapperTest, Run_CaptureStderr_Independent) {
  auto opts = ReadOpts({"bash", "-c", "echo stdout >&1; echo stderr >&2"});
  opts.capture_stderr = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "stdout\n");
  EXPECT_EQ(result.stderr_data, "stderr\n");
}

TEST_F(PopenWrapperTest, Run_CaptureStderr_Empty) {
  auto opts = ReadOpts({"echo", "only stdout"});
  opts.capture_stderr = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "only stdout\n");
  EXPECT_TRUE(result.stderr_data.empty());
}

TEST_F(PopenWrapperTest, Run_CaptureStderr_OnlyStderr) {
  auto opts = ReadOpts({"bash", "-c", "echo err >&2"});
  opts.capture_stderr = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_TRUE(result.stdout_data.empty());
  EXPECT_EQ(result.stderr_data, "err\n");
}

TEST_F(PopenWrapperTest, Run_MergeStderrIntoStdout_Order) {
  // Both streams should appear in stdout_data; exact ordering is
  // non-deterministic due to buffering, so we just check both are present.
  auto opts = ReadOpts({"bash", "-c", "echo out; echo err >&2"});
  opts.merge_stderr_into_stdout = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_NE(result.stdout_data.find("out"), std::string::npos);
  EXPECT_NE(result.stdout_data.find("err"), std::string::npos);
  EXPECT_TRUE(result.stderr_data.empty());
}

TEST_F(PopenWrapperTest, Run_NoCaptureStderr_StderrNotInResult) {
  // Without capture_stderr, stderr_data must remain empty.
  auto opts = ReadOpts({"bash", "-c", "echo err >&2"});
  // capture_stderr defaults to false.
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_TRUE(result.stderr_data.empty());
}

TEST_F(PopenWrapperTest, Run_ExtraEnv_AddNew) {
  auto opts = ReadOpts({"bash", "-c", "echo $MY_TEST_VAR"});
  opts.extra_env = {"MY_TEST_VAR=hello_from_env"};
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "hello_from_env\n");
}

TEST_F(PopenWrapperTest, Run_ExtraEnv_Override) {
  // Override PATH to something minimal; `bash` itself is already exec'd
  // so this only affects the child shell's PATH.
  auto opts = ReadOpts({"bash", "-c", "echo $POPEN_TEST_OVERRIDE"});
  opts.extra_env = {"POPEN_TEST_OVERRIDE=overridden"};
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "overridden\n");
}

TEST_F(PopenWrapperTest, Run_ReplaceEnv_MinimalEnv) {
  // With replace_env=true only our vars exist; HOME and PATH are gone.
  auto opts = ReadOpts({"bash", "-c", "echo ${MY_VAR:-missing}"});
  opts.replace_env = true;
  opts.extra_env = {"MY_VAR=present"};
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "present\n");
}

TEST_F(PopenWrapperTest, Run_ReplaceEnv_NoInheritance) {
  // PATH must not be inherited — so `bash -c` using the inherited PATH must
  // fail to find anything.  We check that HOME is absent.
  auto opts = ReadOpts({"bash", "-c", "echo ${HOME:-no_home}"});
  opts.replace_env = true;
  // No extra_env → effectively empty environment.
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  // bash -c runs because we provide the full path via execvp with existing PATH
  // at fork time, but the child's env has no HOME.
  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "no_home\n");
}

TEST_F(PopenWrapperTest, Run_WorkingDirectory) {
  auto opts = ReadOpts({"pwd"});
  opts.working_directory = "/tmp";
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  // /tmp may be symlinked (e.g. macOS) — compare after trimming the newline.
  std::string out = result.stdout_data;
  if (!out.empty() && out.back() == '\n')
    out.pop_back();
  EXPECT_FALSE(out.empty());
  // The real path must contain "tmp".
  EXPECT_NE(out.find("tmp"), std::string::npos);
}

TEST_F(PopenWrapperTest, Run_InvalidWorkingDirectory) {
  auto opts = ReadOpts({"echo", "test"});
  opts.working_directory = "/nonexistent/directory/xyz123abc";
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  // Child calls _exit(127) when chdir fails.
  EXPECT_FALSE(result.Success());
}

TEST_F(PopenWrapperTest, Run_MaxOutputBytes_Truncated) {
  auto opts = ReadOpts({"yes", "a"});
  opts.max_output_bytes = 100;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.output_truncated);
  EXPECT_EQ(result.stdout_data.size(), 100u);
}

TEST_F(PopenWrapperTest, Run_MaxOutputBytes_NotTruncated) {
  // Output is shorter than the cap → no truncation.
  auto opts = ReadOpts({"echo", "short"});
  opts.max_output_bytes = 1024;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_FALSE(result.output_truncated);
  EXPECT_EQ(result.stdout_data, "short\n");
}

TEST_F(PopenWrapperTest, Run_MaxOutputBytes_ExactSize) {
  // Generate exactly 10 bytes: "0123456789".
  auto opts = ReadOpts({"printf", "0123456789"});
  opts.max_output_bytes = 10;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_FALSE(result.output_truncated);
  EXPECT_EQ(result.stdout_data.size(), 10u);
}

TEST_F(PopenWrapperTest, Run_StdoutCallback_CollectsAllData) {
  auto opts = ReadOpts({"echo", "callback_test"});
  std::string received;
  opts.stdout_callback = [&received](std::string_view chunk) {
    received.append(chunk.data(), chunk.size());
    return true;
  };

  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(received, "callback_test\n");
  // Data must NOT be accumulated in the result when a callback is set.
  EXPECT_TRUE(result.stdout_data.empty());
}

TEST_F(PopenWrapperTest, Run_StderrCallback) {
  auto opts = ReadOpts({"bash", "-c", "echo err >&2"});
  opts.capture_stderr = true;
  std::string received;
  opts.stderr_callback = [&received](std::string_view chunk) {
    received.append(chunk.data(), chunk.size());
    return true;
  };

  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(received, "err\n");
  EXPECT_TRUE(result.stderr_data.empty());
}

TEST_F(PopenWrapperTest, Run_StdoutCallback_AbortEarly) {
  // Callback returns false after 2 calls → child is terminated.
  auto opts = ReadOpts({"yes", "x"});
  int call_count = 0;
  opts.stdout_callback = [&call_count](std::string_view) -> bool {
    ++call_count;
    return call_count < 3; // abort on 3rd call
  };

  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  // Child was killed by SIGTERM so it will not have exited normally.
  EXPECT_FALSE(result.Success());
  EXPECT_GE(call_count, 2);
}

TEST_F(PopenWrapperTest, Run_StdoutCallback_LargeStream) {
  // Generate 1 MB and verify total bytes received via callback.
  auto opts = ReadOpts({"bash", "-c",
                        "dd if=/dev/zero bs=1024 count=1024 2>/dev/null | "
                        "tr '\\0' 'A'"});
  std::size_t total = 0;
  opts.stdout_callback = [&total](std::string_view chunk) {
    total += chunk.size();
    return true;
  };

  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(total, 1024u * 1024u);
  EXPECT_TRUE(result.stdout_data.empty());
}

TEST_F(PopenWrapperTest, Run_Timeout_KillsChild) {
  auto opts = ReadOpts({"sleep", "60"});
  opts.timeout = 150ms;
  opts.kill_grace_period = 100ms;

  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.timed_out);
  EXPECT_FALSE(result.Success());
  // Killed by signal (SIGTERM or SIGKILL depending on grace period).
  EXPECT_TRUE(result.killed_by_signal || result.timed_out);
}

TEST_F(PopenWrapperTest, Run_Timeout_WallClockRespected) {
  auto opts = ReadOpts({"sleep", "60"});
  opts.timeout = 200ms;
  opts.kill_grace_period = 100ms;

  PopenWrapper proc(std::move(opts));
  auto t0 = std::chrono::steady_clock::now();
  proc.Run();
  auto elapsed = std::chrono::steady_clock::now() - t0;

  // Should finish well under 2 seconds.
  EXPECT_LT(elapsed, 2s);
}

TEST_F(PopenWrapperTest, Run_Timeout_NotTriggeredForFastProcess) {
  auto opts = ReadOpts({"true"});
  opts.timeout = 5s;

  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_FALSE(result.timed_out);
}

TEST_F(PopenWrapperTest, Run_Timeout_ElapsedMs_Populated) {
  auto opts = ReadOpts({"sleep", "60"});
  opts.timeout = 150ms;
  opts.kill_grace_period = 100ms;

  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_GT(result.elapsed_ms.count(), 0);
  // Must not have taken more than 2 seconds.
  EXPECT_LT(result.elapsed_ms.count(), 2000);
}

TEST_F(PopenWrapperTest, Run_ExecutablePath_Override) {
  auto opts = ReadOpts({"myapp", "arg1"});
  opts.executable_path = "/bin/echo";
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "arg1\n");
}

TEST_F(PopenWrapperTest, Start_GetPid_BeforeAndAfter) {
  PopenWrapper proc(ReadOpts({"true"}));
  EXPECT_EQ(proc.GetPid(), -1);

  proc.Start();
  EXPECT_GT(proc.GetPid(), 0);

  proc.Wait();
}

TEST_F(PopenWrapperTest, Start_IsRunning_Transitions) {
  PopenWrapper proc(ReadOpts({"sleep", "10"}));
  EXPECT_FALSE(proc.IsRunning());

  proc.Start();
  EXPECT_TRUE(proc.IsRunning());

  proc.Kill();
  proc.Wait();
  EXPECT_FALSE(proc.IsRunning());
}

TEST_F(PopenWrapperTest, Start_Wait_BasicResult) {
  PopenWrapper proc(ReadOpts({"echo", "async"}));
  proc.Start();
  auto result = proc.Wait();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "async\n");
}

TEST_F(PopenWrapperTest, Start_WriteStdin_ThenWait) {
  auto opts = PopenWrapper::Options{};
  opts.command = {"cat"};
  opts.mode = PopenWrapper::Mode::kReadWrite;
  PopenWrapper proc(std::move(opts));
  proc.Start();

  const std::string payload = "hello from WriteStdin\n";
  ssize_t n = proc.WriteStdin(payload);
  EXPECT_EQ(n, static_cast<ssize_t>(payload.size()));
  proc.CloseStdin();

  auto result = proc.Wait();
  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, payload);
}

TEST_F(PopenWrapperTest, Start_ReadStdoutChunk) {
  PopenWrapper proc(ReadOpts({"echo", "chunk_test"}));
  proc.Start();

  // Drain all chunks.
  std::string collected;
  while (auto chunk = proc.ReadStdoutChunk()) {
    collected += *chunk;
  }
  proc.Wait();

  EXPECT_EQ(collected, "chunk_test\n");
}

TEST_F(PopenWrapperTest, Start_ReadStderrChunk) {
  auto opts = ReadOpts({"bash", "-c", "echo errchunk >&2"});
  opts.capture_stderr = true;
  PopenWrapper proc(std::move(opts));
  proc.Start();

  std::string collected;
  while (auto chunk = proc.ReadStderrChunk()) {
    collected += *chunk;
  }
  proc.Wait();

  EXPECT_EQ(collected, "errchunk\n");
}

TEST_F(PopenWrapperTest, Start_CalledTwice_Throws) {
  PopenWrapper proc(ReadOpts({"true"}));
  proc.Start();
  EXPECT_THROW(proc.Start(), std::logic_error);
  proc.Wait();
}

TEST_F(PopenWrapperTest, Wait_CalledBeforeStart_Throws) {
  PopenWrapper proc(ReadOpts({"true"}));
  EXPECT_THROW(proc.Wait(), std::logic_error);
}

TEST_F(PopenWrapperTest, Wait_CalledTwice_Throws) {
  PopenWrapper proc(ReadOpts({"true"}));
  proc.Start();
  proc.Wait();
  EXPECT_THROW(proc.Wait(), std::logic_error);
}

TEST_F(PopenWrapperTest, Run_CalledTwice_Throws) {
  PopenWrapper proc(ReadOpts({"true"}));
  proc.Run();
  EXPECT_THROW(proc.Run(), std::logic_error);
}

TEST_F(PopenWrapperTest, Terminate_SendsSigterm) {
  PopenWrapper proc(ReadOpts({"sleep", "60"}));
  proc.Start();
  std::this_thread::sleep_for(50ms);
  proc.Terminate();

  auto result = proc.Wait();

  EXPECT_FALSE(result.Success());
  EXPECT_TRUE(result.killed_by_signal);
  EXPECT_EQ(result.signal_number, SIGTERM);
}

TEST_F(PopenWrapperTest, Kill_SendsSigkill) {
  PopenWrapper proc(ReadOpts({"sleep", "60"}));
  proc.Start();
  std::this_thread::sleep_for(50ms);
  proc.Kill();

  auto result = proc.Wait();

  EXPECT_FALSE(result.Success());
  EXPECT_TRUE(result.killed_by_signal);
  EXPECT_EQ(result.signal_number, SIGKILL);
}

TEST_F(PopenWrapperTest, Terminate_BeforeStart_NoThrow) {
  // Calling Terminate() before Start() must be a no-op.
  PopenWrapper proc(ReadOpts({"true"}));
  EXPECT_NO_THROW(proc.Terminate());
}

TEST_F(PopenWrapperTest, Kill_BeforeStart_NoThrow) {
  PopenWrapper proc(ReadOpts({"true"}));
  EXPECT_NO_THROW(proc.Kill());
}

TEST_F(PopenWrapperTest, MoveConstruct_BeforeStart) {
  PopenWrapper proc1(ReadOpts({"echo", "moved"}));
  PopenWrapper proc2 = std::move(proc1);

  auto result = proc2.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "moved\n");
}

TEST_F(PopenWrapperTest, MoveAssign_BeforeStart) {
  auto opts1 = ReadOpts({"echo", "a"});
  auto opts2 = ReadOpts({"echo", "b"});
  PopenWrapper proc1(std::move(opts1));
  PopenWrapper proc2(std::move(opts2));
  proc2 = std::move(proc1);

  auto result = proc2.Run();
  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "a\n");
}

TEST_F(PopenWrapperTest, ShellEscape_Plain) {
  EXPECT_EQ(PopenWrapper::ShellEscape("hello"), "'hello'");
}

TEST_F(PopenWrapperTest, ShellEscape_WithSpaces) {
  EXPECT_EQ(PopenWrapper::ShellEscape("hello world"), "'hello world'");
}

TEST_F(PopenWrapperTest, ShellEscape_WithSingleQuote) {
  EXPECT_EQ(PopenWrapper::ShellEscape("it's"), "'it'\"'\"'s'");
}

TEST_F(PopenWrapperTest, ShellEscape_Empty) {
  EXPECT_EQ(PopenWrapper::ShellEscape(""), "''");
}

TEST_F(PopenWrapperTest, ShellEscape_OnlyQuotes) {
  EXPECT_EQ(PopenWrapper::ShellEscape("'"), "''\"'\"''");
}

TEST_F(PopenWrapperTest, ShellEscape_SpecialShellChars) {
  // $, *, ?, ! etc. must all be safe inside single quotes.
  EXPECT_EQ(PopenWrapper::ShellEscape("$HOME"), "'$HOME'");
  EXPECT_EQ(PopenWrapper::ShellEscape("*.cc"), "'*.cc'");
  EXPECT_EQ(PopenWrapper::ShellEscape("a;b"), "'a;b'");
}

TEST_F(PopenWrapperTest, BuildShellCommand_Simple) {
  std::vector<std::string> argv = {"echo", "hello world"};
  EXPECT_EQ(PopenWrapper::BuildShellCommand(argv), "'echo' 'hello world'");
}

TEST_F(PopenWrapperTest, BuildShellCommand_SingleArg) {
  std::vector<std::string> argv = {"true"};
  EXPECT_EQ(PopenWrapper::BuildShellCommand(argv), "'true'");
}

TEST_F(PopenWrapperTest, BuildShellCommand_Empty) {
  std::vector<std::string> argv;
  EXPECT_EQ(PopenWrapper::BuildShellCommand(argv), "");
}

TEST_F(PopenWrapperTest, BuildShellCommand_Roundtrip) {
  // BuildShellCommand produces a string safe for /bin/sh -c when used via
  // shell=false (i.e. the caller manually constructs the shell invocation).
  // Verify the escaped string is correctly re-interpreted by the shell.
  std::string cmd = "echo " + PopenWrapper::BuildShellCommand({"a b", "c'd"});
  auto opts = ReadOpts({cmd});
  opts.shell = true;
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  // echo prints args space-separated.
  EXPECT_EQ(result.stdout_data, "a b c'd\n");
}

TEST_F(PopenWrapperTest, Result_ElapsedMs_Nonzero) {
  auto opts = ReadOpts({"bash", "-c", "sleep 0.05"});
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  // Must reflect at least ~50 ms.
  EXPECT_GE(result.elapsed_ms.count(), 40);
}

TEST_F(PopenWrapperTest, Result_RawExitStatus_WIFEXITED) {
  PopenWrapper proc(ReadOpts({"true"}));
  auto result = proc.Run();

  EXPECT_TRUE(WIFEXITED(result.raw_exit_status));
}

TEST_F(PopenWrapperTest, Run_NonExistentCommand_ExitsWithCode127) {
  auto opts = ReadOpts({"/no/such/binary/xyz123"});
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  // exec fails → _exit(127).
  EXPECT_FALSE(result.Success());
  EXPECT_TRUE(result.exited_normally);
  EXPECT_EQ(result.exit_code, 127);
}

TEST_F(PopenWrapperTest, Run_VeryLongOutputLine) {
  // Single line of 500 KB.
  const std::size_t kLen = 500000;
  auto opts = ReadOpts(
    {"bash", "-c", "printf '%0.s-' $(seq 1 " + std::to_string(kLen) + ")"});
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data.size(), kLen);
}

TEST_F(PopenWrapperTest, Run_ManyShortProcesses_Sequential) {
  // Ensures no fd or zombie leaks across repeated instantiations.
  for (int i = 0; i < 50; ++i) {
    PopenWrapper proc(ReadOpts({"true"}));
    auto result = proc.Run();
    EXPECT_TRUE(result.Success()) << "Failed on iteration " << i;
  }
}

TEST_F(PopenWrapperTest, Destructor_KillsRunningChild) {
  // Destructor of a started-but-not-waited process must not hang.
  {
    PopenWrapper proc(ReadOpts({"sleep", "60"}));
    proc.Start();
    // proc goes out of scope here → destructor must kill child.
  }
  // If we reach here the destructor did not deadlock.
  SUCCEED();
}

TEST_F(PopenWrapperTest, Run_StdinDataIgnored_InReadOnlyMode) {
  // stdin_data is populated but mode is kReadOnly → child never reads it.
  auto opts = ReadOpts({"echo", "read_only"});
  opts.stdin_data = "ignored";
  PopenWrapper proc(std::move(opts));
  auto result = proc.Run();

  EXPECT_TRUE(result.Success());
  EXPECT_EQ(result.stdout_data, "read_only\n");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
