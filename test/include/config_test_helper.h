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
            if (line != nullptr)
            {
                data_.append(line);
            }
        }
    }

    explicit ConfigLineStream(const std::vector<std::string> &lines)
    {
        for (const std::string &line : lines)
        {
            data_.append(line);
        }
    }

    explicit ConfigLineStream(std::string raw_bytes) : data_(std::move(raw_bytes)) {}

    static FILE *handle()
    {
        return reinterpret_cast<FILE *>(static_cast<uintptr_t>(0x1));
    }

    void setChunkLimit(size_t max_bytes)
    {
        chunk_limit_ = max_bytes;
    }

    size_t readBytes(void *buf, size_t size, size_t nmemb, FILE *stream)
    {
        size_t max_elements;
        size_t remaining;
        size_t avail_elements;
        size_t copy_elements;
        size_t copy_bytes;

        if (buf == nullptr || size == 0U || nmemb == 0U || stream != handle())
        {
            return 0U;
        }

        if (offset_ >= data_.size())
        {
            return 0U;
        }

        remaining = data_.size() - offset_;
        max_elements = nmemb;
        if (chunk_limit_ > 0U && chunk_limit_ / size < max_elements)
        {
            max_elements = chunk_limit_ / size;
            if (max_elements == 0U)
            {
                max_elements = 1U;
            }
        }

        avail_elements = remaining / size;
        copy_elements = (avail_elements < max_elements) ? avail_elements : max_elements;
        copy_bytes = copy_elements * size;
        if (copy_bytes == 0U && size == 1U && remaining > 0U)
        {
            copy_elements = 1U;
            copy_bytes = 1U;
        }

        if (copy_bytes > 0U)
        {
            memcpy(buf, data_.data() + offset_, copy_bytes);
            offset_ += copy_bytes;
        }

        return copy_elements;
    }

  private:
    std::string data_;
    size_t offset_ = 0U;
    size_t chunk_limit_ = 0U;
};

#endif /* PORTER_CONFIG_TEST_HELPER_H */
