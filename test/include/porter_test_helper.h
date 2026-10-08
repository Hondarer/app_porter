#ifndef PORTER_TEST_HELPER_H
#define PORTER_TEST_HELPER_H

#include <cplat/base/platform.h>
#include <cplat/crt/path.h>
#include <cplat/crt/stdio.h>
#include <cplat/crt/unistd.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(PLATFORM_LINUX)
    #include <unistd.h>
#endif /* PLATFORM_LINUX */

/**
 * テスト用 porter サービス定義を一時ファイルに書き出すビルダー。
 * デストラクターで一時ファイルを削除する。
 *
 * 使用例:
 *   PorterConfigBuilder cfg;
 *   std::string config_path = cfg.addUnicastService(10, 19010).build();
 */
class PorterConfigBuilder
{
  public:
    PorterConfigBuilder() = default;

    PorterConfigBuilder &setUdpHealthIntervalMs(uint32_t value)
    {
        udp_health_interval_ms_ = value;
        return *this;
    }

    PorterConfigBuilder &setUdpHealthTimeoutMs(uint32_t value)
    {
        udp_health_timeout_ms_ = value;
        return *this;
    }

    PorterConfigBuilder &setTcpHealthIntervalMs(uint32_t value)
    {
        tcp_health_interval_ms_ = value;
        return *this;
    }

    PorterConfigBuilder &setTcpHealthTimeoutMs(uint32_t value)
    {
        tcp_health_timeout_ms_ = value;
        return *this;
    }

    PorterConfigBuilder &setTcpCloseTimeoutMs(uint32_t value)
    {
        tcp_close_timeout_ms_ = value;
        return *this;
    }

    PorterConfigBuilder &addUnicastService(int64_t id, int port, const std::string &host = "127.0.0.1",
                                           const std::string &key = "")
    {
        return addService(id, "unicast", port, host, key, false, 0);
    }

    PorterConfigBuilder &addUnicastBidirService(int64_t id, int port, const std::string &host = "127.0.0.1",
                                                const std::string &key = "")
    {
        return addService(id, "unicast_bidir", port, host, key, false, 0);
    }

    PorterConfigBuilder &addUnicastBidirN1Service(int64_t id, int port, int max_peers,
                                                  const std::string &bind_addr = "0.0.0.0", const std::string &key = "")
    {
        return addService(id, "unicast_bidir_n1", port, bind_addr, key, false, max_peers);
    }

    PorterConfigBuilder &addTcpBidirService(int64_t id, int port, const std::string &host = "127.0.0.1",
                                            const std::string &key = "")
    {
        return addService(id, "tcp_bidir", port, host, key, true, 0);
    }

    /**
     * 一時ファイルを書き出し、そのパスを返す。
     * 2 回目以降の呼び出しでは既存ファイルを上書きする。
     */
    std::string build()
    {
        if (tmp_path_.empty())
        {
#if defined(PLATFORM_LINUX)
            char tmpl[] = "/tmp" PLATFORM_PATH_SEP "porter_test_XXXXXX.json";
            int fd = mkstemps(tmpl, 5); /* ".json" = 5 文字 */
            if (fd == -1)
            {
                return "";
            }
            cplat_close(fd, nullptr);
            tmp_path_ = tmpl;
#elif defined(PLATFORM_WINDOWS)
            char tmp_dir[PLATFORM_PATH_MAX] = {};
            GetTempPathA(sizeof(tmp_dir), tmp_dir);
            char tmp_file[PLATFORM_PATH_MAX] = {};
            GetTempFileNameA(tmp_dir, "ptr", 0, tmp_file);
            /* .json 拡張子に変更 */
            tmp_path_ = std::string(tmp_file) + ".json";
            /* GetTempFileName が作成した元ファイルを削除して .json で作り直す */
            DeleteFileA(tmp_file);
#endif /* PLATFORM_ */
        }

        FILE *f = cplat_fopen(tmp_path_.c_str(), "w", nullptr);
        if (f == nullptr)
        {
            return "";
        }

        cplat_fprintf(f, "// テスト用の共通設定\n{\"global\":{\"window_size\":16,\"max_payload\":1400,");
        cplat_fprintf(f, "\"udp_health_interval_ms\":%u,", udp_health_interval_ms_);
        cplat_fprintf(f, "\"udp_health_timeout_ms\":%u,", udp_health_timeout_ms_);
        cplat_fprintf(f, "\"tcp_health_interval_ms\":%u,", tcp_health_interval_ms_);
        cplat_fprintf(f, "\"tcp_health_timeout_ms\":%u,", tcp_health_timeout_ms_);
        cplat_fprintf(f, "\"tcp_close_timeout_ms\":%u},\"services\":{", tcp_close_timeout_ms_);
        for (size_t i = 0; i < services_.size(); i++)
        {
            cplat_fprintf(f, "%s%s", i == 0 ? "" : ",", services_[i].c_str());
        }
        cplat_fprintf(f, "}}\n");
        fclose(f);

        return tmp_path_;
    }

    ~PorterConfigBuilder()
    {
        if (!tmp_path_.empty())
        {
#if defined(PLATFORM_LINUX)
            unlink(tmp_path_.c_str());
#elif defined(PLATFORM_WINDOWS)
            DeleteFileA(tmp_path_.c_str());
#endif /* PLATFORM_ */
        }
    }

    /* コピー禁止 */
    PorterConfigBuilder(const PorterConfigBuilder &) = delete;
    PorterConfigBuilder &operator=(const PorterConfigBuilder &) = delete;

  private:
    uint32_t udp_health_interval_ms_ = 1000U;
    uint32_t udp_health_timeout_ms_ = 3000U;
    uint32_t tcp_health_interval_ms_ = 1000U;
    uint32_t tcp_health_timeout_ms_ = 3000U;
    PorterConfigBuilder &addService(int64_t id, const std::string &type, int port, const std::string &host,
                                    const std::string &key, bool tcp, int max_peers)
    {
        std::string entry = "\"" + std::to_string(id) + "\":{\"type\":\"" + type +
                            "\",\"dst_port\":" + std::to_string(port) + ",\"dst_addr1\":\"" + host + "\"";
        if (!tcp && max_peers == 0)
        {
            entry += ",\"src_addr1\":\"" + host + "\"";
        }
        if (max_peers > 0)
        {
            entry += ",\"max_peers\":" + std::to_string(max_peers);
        }
        if (!key.empty())
        {
            entry += ",\"encrypt_key\":\"" + key + "\"";
        }
        services_.push_back(entry + "}");
        return *this;
    }
    std::vector<std::string> services_;
    std::string tmp_path_;
    uint32_t tcp_close_timeout_ms_ = 5000U;
    uint32_t _pad_tcp_close_timeout_ = 0U;
};

#endif /* PORTER_TEST_HELPER_H */
