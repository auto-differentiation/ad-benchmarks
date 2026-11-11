// One CSV line per benchmark result. Numbers are formatted with std::to_chars,
// so the output never depends on the global or stream locale.

#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace harness {

struct Record {
    std::string library;
    std::string workload;
    std::size_t size = 0;
    double median_s = 0.0;
    double iqr_s = 0.0;
    double plain_median_s = 0.0;
    double ratio = 0.0;
    std::uint64_t peak_memory_bytes = 0;
    std::uint64_t peak_memory_growth_bytes = 0;  // peak minus the run's baseline
    std::uint64_t peak_heap_reserved_bytes = 0;  // heap allocated, touched or not
    std::string library_version;
    std::string compiler;
    std::string cpu;
    std::string date;
    std::string git_sha;
};

class CsvWriter {
public:
    explicit CsvWriter(std::ostream& out) : out_(out) {}

    void row(const Record& r)
    {
        if (!header_written_) {
            out_ << "library,workload,size,median_s,iqr_s,plain_median_s,ratio,"
                    "peak_memory_bytes,peak_memory_growth_bytes,peak_heap_reserved_bytes,library_version,compiler,cpu,date,git_sha\n";
            header_written_ = true;
        }
        out_ << field(r.library) << ',' << field(r.workload) << ',' << number(static_cast<std::uint64_t>(r.size)) << ','
             << number(r.median_s) << ',' << number(r.iqr_s) << ','
             << number(r.plain_median_s) << ',' << number(r.ratio) << ','
             << number(r.peak_memory_bytes) << ','
             << number(r.peak_memory_growth_bytes) << ','
             << number(r.peak_heap_reserved_bytes) << ',' << field(r.library_version) << ','
             << field(r.compiler) << ',' << field(r.cpu) << ',' << field(r.date) << ','
             << field(r.git_sha) << '\n';
    }

private:
    // RFC 4180: quote fields containing a comma, double quote, CR or LF,
    // doubling embedded quotes.
    static std::string field(const std::string& s)
    {
        if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
        std::string q = "\"";
        for (char c : s) {
            if (c == '"') q += '"';
            q += c;
        }
        q += '"';
        return q;
    }

    // Same digits as printf "%.17g", always with '.' as decimal point.
    static std::string number(double x)
    {
        char buf[32];
        const auto res = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::general, 17);
        return std::string(buf, res.ptr);
    }

    static std::string number(std::uint64_t x)
    {
        char buf[24];
        const auto res = std::to_chars(buf, buf + sizeof buf, x);
        return std::string(buf, res.ptr);
    }

    std::ostream& out_;
    bool header_written_ = false;
};

}  // namespace harness
