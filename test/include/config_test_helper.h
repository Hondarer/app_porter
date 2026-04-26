#ifndef PORTER_CONFIG_TEST_HELPER_H
#define PORTER_CONFIG_TEST_HELPER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

class ConfigLineStream
{
  public:
    ConfigLineStream(std::initializer_list<const char *> lines)
    {
        for (const char *line : lines)
        {
            lines_.emplace_back(line);
        }
    }

    explicit ConfigLineStream(const std::vector<std::string> &lines) : lines_(lines)
    {
    }

    static FILE *handle()
    {
        return reinterpret_cast<FILE *>(static_cast<uintptr_t>(0x1));
    }

    char *read(char *buf, int size, FILE *stream)
    {
        size_t len;

        if (buf == nullptr || size <= 0 || stream != handle())
        {
            return nullptr;
        }

        if (next_ >= lines_.size())
        {
            return nullptr;
        }

        len = lines_[next_].size();
        if (len >= (size_t)size)
        {
            len = (size_t)size - 1U;
        }

        memcpy(buf, lines_[next_].data(), len);
        buf[len] = '\0';
        next_++;
        return buf;
    }

  private:
    std::vector<std::string> lines_;
    size_t                   next_ = 0U;
};

#endif /* PORTER_CONFIG_TEST_HELPER_H */
