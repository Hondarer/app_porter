#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <config.h>
#include <mock_com_util.h>
#include <porter_type.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(PLATFORM_LINUX)
    #include <unistd.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */

using namespace testing;

namespace
{
FILE *delegate_fopen(const char *path, const char *modes, int *errno_out)
{
    FILE *fp;

    errno = 0;
    fp    = fopen(path, modes);
    if (fp == nullptr && errno_out != nullptr)
    {
        *errno_out = errno;
    }

    return fp;
}

class TempConfigFile
{
  public:
    TempConfigFile()
    {
#if defined(PLATFORM_LINUX)
        char tmpl[] = "/tmp/porter_config_XXXXXX.conf";
        int fd = mkstemps(tmpl, 5);
        if (fd != -1)
        {
            close(fd);
            path_ = tmpl;
        }
#elif defined(PLATFORM_WINDOWS)
        char tmp_dir[MAX_PATH] = {};
        char tmp_file[MAX_PATH] = {};
        GetTempPathA(sizeof(tmp_dir), tmp_dir);
        GetTempFileNameA(tmp_dir, "pcg", 0, tmp_file);
        path_ = std::string(tmp_file) + ".conf";
        DeleteFileA(tmp_file);
#endif /* PLATFORM_ */
    }

    ~TempConfigFile()
    {
        if (!path_.empty())
        {
#if defined(PLATFORM_LINUX)
            unlink(path_.c_str());
#elif defined(PLATFORM_WINDOWS)
            DeleteFileA(path_.c_str());
#endif /* PLATFORM_ */
        }
    }

    bool writeAll(const char *text) const
    {
        FILE *fp = nullptr;
#if defined(PLATFORM_LINUX)
        fp = fopen(path_.c_str(), "w");
#elif defined(PLATFORM_WINDOWS)
        fopen_s(&fp, path_.c_str(), "w");
#endif /* PLATFORM_ */
        if (fp == nullptr)
        {
            return false;
        }
        size_t len = strlen(text);
        size_t wr = fwrite(text, 1, len, fp);
        fclose(fp);
        return wr == len;
    }

    const char *path() const
    {
        return path_.c_str();
    }

  private:
    std::string path_;
};
} /* namespace */

TEST(configLoadGlobalTest, loadsProtocolSpecificHealthDefaults)
{
    NiceMock<Mock_com_util> mock_com_util;
    TempConfigFile file;
    PotrGlobalConfig global = {};

    ON_CALL(mock_com_util, com_util_fopen(_, _, _))
        .WillByDefault(Invoke(delegate_fopen));

    ASSERT_NE('\0', file.path()[0]);
    ASSERT_TRUE(file.writeAll("[global]\n"
                              "udp_health_interval_ms = 111\n"
                              "udp_health_timeout_ms = 222\n"
                              "tcp_health_interval_ms = 333\n"
                              "tcp_health_timeout_ms = 444\n"));

    ASSERT_EQ(POTR_SUCCESS, config_load_global(file.path(), &global));
    EXPECT_EQ(111U, global.udp_health_interval_ms);
    EXPECT_EQ(222U, global.udp_health_timeout_ms);
    EXPECT_EQ(333U, global.tcp_health_interval_ms);
    EXPECT_EQ(444U, global.tcp_health_timeout_ms);
}
