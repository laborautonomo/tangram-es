// Graciously stolen from https://github.com/cpp-netlib/uri/blob/master/include/network/uri/uri.hpp
// See also http://opensource.apple.com//source/CF/CF-476.15/CFURL.h

#pragma once

#include <string>

namespace Tangram {

class Uri {

public:

    Uri(const std::string& source);

    bool isAbsolute() const;
    bool hasScheme() const;
    bool hasUser() const;
    bool hasHost() const;
    bool hasPort() const;
    bool hasPath() const;
    bool hasQuery() const;
    bool hasFragment() const;

    std::string scheme() const;
    std::string user() const;
    std::string host() const;
    std::string port() const;
    std::string path() const;
    std::string query() const;
    std::string fragment() const;

    const std::string& string() const;

    bool empty() const;

    int portNumber() const;

    Uri makeRelative(const Uri& base) const;

    Uri resolve(const Uri& base) const;

    bool operator==(const Uri& rhs) const;

private:

    std::string m_string;

    struct {
        size_t start = 0, count = 0;
    } m_scheme, m_user, m_host, m_port, m_path, m_query, m_fragment;

    bool m_absolute = false;

};

}