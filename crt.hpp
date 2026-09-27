#include <cstdarg>
#include <wtypes.h>
namespace crt {
    static void write_string(const char* text, unsigned __int64 size)
    {
        DWORD written;
        WriteFile(
            GetStdHandle(STD_OUTPUT_HANDLE),
            text,
            static_cast<DWORD>(size),
            &written,
            0
        );
    }

    static unsigned __int64 string_length(const char* text)
    {
        unsigned __int64 size = 0;

        while (text[size])
            ++size;

        return size;
    }

    static void write_uint(unsigned __int64 value)
    {
        char buffer[32];
        unsigned __int64 size = 0;

        do
        {
            buffer[size++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);

        while (size)
            write_string(&buffer[--size], 1);
    }

    static void write_int(long long value)
    {
        if (value < 0)
        {
            write_string("-", 1);
            write_uint(static_cast<unsigned __int64>(-value));
        }
        else
        {
            write_uint(static_cast<unsigned __int64>(value));
        }
    }

    int printf(const char* format, ...)
    {
        va_list args;
        va_start(args, format);

        int written = 0;

        while (*format)
        {
            if (*format != '%')
            {
                write_string(format++, 1);
                ++written;
                continue;
            }

            ++format;

            switch (*format)
            {
            case '%':
                write_string("%", 1);
                ++written;
                break;

            case 'c':
            {
                char value = static_cast<char>(va_arg(args, int));
                write_string(&value, 1);
                ++written;
                break;
            }

            case 's':
            {
                const char* value = va_arg(args, const char*);
                unsigned __int64 size = string_length(value);

                write_string(value, size);
                written += static_cast<int>(size);
                break;
            }

            case 'd':
            {
                long value = va_arg(args, long);
                write_int(value);
                break;
            }

            case 'u':
            {
                unsigned long value = va_arg(args, unsigned long);
                write_uint(value);
                break;
            }

            case 'l':
                if (format[1] == 'l' && format[2] == 'u')
                {
                    unsigned long long value =
                        va_arg(args, unsigned long long);

                    write_uint(value);
                    format += 2;
                }
                break;

            default:
                write_string("%", 1);
                write_string(format, 1);
                written += 2;
                break;
            }

            ++format;
        }

        va_end(args);
        return written;
    }
}