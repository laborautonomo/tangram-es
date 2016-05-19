// Graciously stolen from https://github.com/cpp-netlib/uri/blob/master/src/uri.cpp

#include "uri.h"
#include <cstdlib>

namespace Tangram {

Uri::Uri(const std::string& source) {

    m_string = source;

    size_t i = 0;
    auto end = m_string.size();
    auto npos = std::string::npos;

    // Parse the scheme.
    {
        m_scheme.start = i;
        auto colon = m_string.find(':', i);
        if (colon != npos) {
            m_scheme.count = colon - i;
            i = colon + 1;
        }
    }

    // Advance past a '//' following the scheme.
    if (i + 1 < end && m_string[i] == '/' && m_string[i + 1] == '/') {
        i += 2;
    }

    // Find the next occurrences of '/' and ':' after the scheme and '://'.
    auto slash = std::min(m_string.find('/', i), end);
    auto colon = std::min(m_string.find(':', i), end);

    // Parse the host.
    m_host.start = i;
    m_host.count = std::min(slash, colon) - i;
    i += m_host.count;

    // Parse the port, if present.
    if (i < end && m_string[i] == ':') {
        i++;
        m_port.start = i;
        m_port.count = std::min(slash, end) - i;
        i += m_port.count;
    }

    // Find the next occurrences of '?' and '#' after the host and port.
    auto qmark = std::min(m_string.find('?', i), end);
    auto pound = std::min(m_string.find('#', i), end);

    // Parse the path.
    m_path.start = i;
    m_path.count = std::min(qmark, pound) - i;

    // Parse the query.
    i = std::min(qmark + 1, end);
    m_query.start = i;
    m_query.count = pound - i;

    // Parse the fragment.
    i = std::min(pound + 1, end);
    m_fragment.start = i;
    m_fragment.count = end - i;

}

bool Uri::isAbsolute() const {
    return m_absolute;
}

bool Uri::hasScheme() const {
    return m_scheme.count != 0;
}

bool Uri::hasUser() const {
    return m_user.count != 0;
}

bool Uri::hasHost() const {
    return m_host.count != 0;
}

bool Uri::hasPort() const {
    return m_port.count != 0;
}

bool Uri::hasPath() const {
    return m_path.count != 0;
}

bool Uri::hasQuery() const {
    return m_query.count != 0;
}

bool Uri::hasFragment() const {
    return m_fragment.count != 0;
}

std::string Uri::scheme() const {
    return std::string(m_string, m_scheme.start, m_scheme.count);
}

std::string Uri::user() const {
    return std::string(m_string, m_user.start, m_user.count);
}

std::string Uri::host() const {
    return std::string(m_string, m_host.start, m_host.count);
}

std::string Uri::port() const {
    return std::string(m_string, m_port.start, m_port.count);
}

std::string Uri::path() const {
    return std::string(m_string, m_path.start, m_path.count);
}

std::string Uri::query() const {
    return std::string(m_string, m_query.start, m_query.count);
}

std::string Uri::fragment() const {
    return std::string(m_string, m_fragment.start, m_fragment.count);
}

const std::string& Uri::string() const {
    return m_string;
}

bool Uri::empty() const {
    return m_string.empty();
}

int Uri::portNumber() const {
    if (hasPort()) {
        char* pos = nullptr;
        long number = std::strtol(&m_string[m_port.start], &pos, 10);
        if (pos == &m_string[m_port.start + m_port.count]) {
            return static_cast<int>(number);
        }
    }
    return -1;
}

Uri Uri::makeRelative(const Uri& base) const {
    // TODO
    return *this;
}

Uri Uri::resolve(const Uri& base) const {
    // TODO
    return *this;
}

bool Uri::operator==(const Uri& rhs) const {
    // TODO
    return false;
}

}
