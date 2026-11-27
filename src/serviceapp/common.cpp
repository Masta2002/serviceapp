#include <dirent.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdint.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "common.h"
#ifndef NO_PYTHON
#include <Python.h>
#endif

SettingEntry::SettingEntry():m_is_set(false), m_int_value(0){}
SettingEntry::SettingEntry(const std::string &app_arg, int value, const std::string value_type):
    m_is_set(true),
    m_app_arg(app_arg), m_int_value(value), m_value_type(value_type){}
SettingEntry::SettingEntry(const std::string &app_arg, const std::string &value, const std::string value_type):
    m_is_set(true),
    m_app_arg(app_arg), m_string_value(value), m_int_value(0), m_value_type(value_type){}
SettingEntry::SettingEntry(const std::string &app_arg, const std::string value_type):
    m_is_set(false),
    m_app_arg(app_arg), m_int_value(0), m_value_type(value_type){}

void SettingEntry::setValue(int value)
{
    m_is_set = true;
    m_int_value = value;
}

void SettingEntry::setValue(std::string value)
{
    if (value.empty())
        return;
    m_is_set = true;
    m_string_value = value;
}

std::string SettingEntry::getAppArg() const
{
    return m_app_arg;
}

std::string SettingEntry::getValue() const
{
    if (m_value_type == "int" || m_value_type == "bool" || m_value_type == "nonzero")
    {
        std::stringstream ss;
        ss << m_int_value;
        return ss.str();
    }
    return m_string_value;
}

int SettingEntry::getValueInt() const { return m_int_value; }

std::string SettingEntry::toString() const
{
    std::stringstream ss;
    if (!m_is_set)
    {
        ss << "not set";
    }
    else
    {
        ss << getValue();
    }
    return ss.str();
}

bool SettingEntry::isSet() const {return m_is_set;}
std::string SettingEntry::getType() const { return m_value_type; }

Url::Url(const std::string& url):
    m_url(url),
    m_port(-1)
{
    parseUrl(url);
}

void Url::parseUrl(std::string url)
{
    size_t delim_start = url.find("://");
    if (delim_start == std::string::npos)
        return;
    size_t fragment_start = url.find("#");
    if (fragment_start != std::string::npos)
    {
        m_fragment = url.substr(fragment_start + 1);
        m_url = url = url.substr(0, url.length() - m_fragment.length() - 1);
    }
    m_proto = url.substr(0, delim_start);

    // the authority ends at the path or, without a path, at the query:
    // "http://host?u=http://x" must not take "host?u=http:" as host
    size_t authority_start = delim_start + 3;
    size_t path_start = url.find_first_of("/?", authority_start);
    std::string authority, path;
    if (path_start != std::string::npos)
    {
        authority = url.substr(authority_start, path_start - authority_start);
        path = url.substr(path_start);
    }
    else
    {
        authority = url.substr(authority_start);
    }
    // user:password@host - the host is what follows the last '@'
    size_t userinfo_end = authority.rfind('@');
    if (userinfo_end != std::string::npos)
    {
        m_userinfo = authority.substr(0, userinfo_end);
        authority = authority.substr(userinfo_end + 1);
    }
    std::string host = authority, port;
    if (!host.empty() && host[0] == '[')
    {
        // IPv6 literal "[2001:db8::1]:8080": its colons are not the port
        size_t literal_end = host.find(']');
        if (literal_end != std::string::npos)
        {
            if (host.compare(literal_end + 1, 1, ":") == 0)
                port = host.substr(literal_end + 2);
            host = host.substr(1, literal_end - 1);
        }
    }
    else
    {
        size_t port_start = host.find(':');
        if (port_start != std::string::npos)
        {
            port = host.substr(port_start + 1);
            host = host.substr(0, port_start);
        }
    }
    // "host:" or a port with garbage keeps the default port of the scheme
    if (!port.empty() && port.size() <= 5 && port.find_first_not_of("0123456789") == std::string::npos)
        m_port = atoi(port.c_str());
    // no path is the root path: the HTTP request line needs one
    if (path.empty() || path[0] == '?')
        path = "/" + path;
    size_t query_start = path.find("?");
    if (query_start != std::string::npos)
    {
        m_query = path.substr(query_start+1);
        path = path.substr(0, query_start);
    }
    m_host = host;
    m_path = path;
}

void splitExtension(const std::string &path, std::string &basename, std::string &extension)
{
    size_t filename_idx = path.find_last_of('/');
    size_t extension_idx = path.find_last_of('.');
    bool has_extension = (extension_idx != std::string::npos
            && (filename_idx == std::string::npos || extension_idx > filename_idx));
    if (has_extension)
    {
        basename = path.substr(0, extension_idx);
        extension = path.substr(extension_idx);
    }
    else
    {
        basename = path;
        extension = "";
    }
}

void splitPath(const std::string &path, std::string &dirpath, std::string &filename)
{
    size_t filename_idx = path.find_last_of('/');
    if (filename_idx != std::string::npos)
    {
        dirpath = path.substr(0, filename_idx);
        filename = path.substr(filename_idx + 1);
    }
    else
    {
        dirpath = "";
        filename = path;
    }
}

int listDir(const std::string &dirpath, std::vector<std::string> *files, std::vector<std::string> *directories)
{
    DIR *dp;
    if ((dp = opendir(dirpath.c_str())) == NULL)
    {
        fprintf(stderr, "listDir(%s) - error in opendir: %m\n",
                dirpath.c_str());
        return -1;
    }

    std::string filepath;
    struct dirent *entry;
    struct stat statbuf;
    while ((entry = readdir(dp)) != NULL)
    {
        if (!strcmp("..", entry->d_name) || !strcmp(".", entry->d_name))
        {
            continue;
        }
        bool isDir;
#ifdef _DIRENT_HAVE_D_TYPE
        // the type from readdir() itself: a movie directory with thousands
        // of files needs no stat() per entry. Only an unknown type (some
        // file systems) and symlinks (the target decides) are looked up.
        if (entry->d_type != DT_UNKNOWN && entry->d_type != DT_LNK)
        {
            isDir = entry->d_type == DT_DIR;
        }
        else
#endif
        {
            if (*dirpath.rbegin() == '/')
                filepath = dirpath + entry->d_name;
            else
                filepath = dirpath + "/" + entry->d_name;
            // a failed stat (broken symlink, entry removed meanwhile) leaves
            // statbuf undefined: skip the entry instead of guessing its type
            if (stat(filepath.c_str(), &statbuf) != 0)
            {
                continue;
            }
            isDir = S_ISDIR(statbuf.st_mode);
        }
        if (isDir)
        {
            if (directories != NULL)
            {
                directories->push_back(entry->d_name);
            }
        }
        else
        {
            if (files != NULL)
            {
                files->push_back(entry->d_name);
            }
        }
    }
    if (closedir(dp) == -1)
    {
        fprintf(stderr, "listDir(%s) - error in closedir: %m\n", dirpath.c_str());
    }
    return 0;
}

static const uint8_t iso8859_2_unused_utf8[10][2] = {
    {0xc2,0x8a},{0xc2,0x8c},{0xc2,0x8d},{0xc2,0x8e},{0xc2,0x8f},
    {0xc2,0x9a},{0xc2,0x9c},{0xc2,0x9d},{0xc2,0x9e},{0xc2,0x9f}};


#ifndef NO_UCHARDET
int detectEncoding(const std::string &content, std::string &encoding)
{
    uchardet_t handle = uchardet_new();
    int retval = uchardet_handle_data(handle, content.c_str(), content.length());
    if (retval != 0)
    {
        fprintf(stderr, "uchardet error: handle data error.\n");
        uchardet_delete(handle);
        return 1;
    }
    uchardet_data_end(handle);
    encoding = (uchardet_get_charset(handle));
    uchardet_delete(handle);
    return 0;
}
#endif

#ifndef NO_PYTHON
int convertToUTF8(const std::string &input_string, const std::string &input_encoding, std::string &output_string)
{
    // decode the raw bytes from input_encoding to str, then take its utf-8 form
    PyObject *py_unicode = PyUnicode_Decode(input_string.data(), input_string.length(), input_encoding.c_str(), "strict");
    if (py_unicode == NULL)
    {
        PyErr_Print();
        return 1;
    }
    Py_ssize_t size = 0;
    const char *utf8 = PyUnicode_AsUTF8AndSize(py_unicode, &size);
    if (utf8 == NULL)
    {
        Py_DECREF(py_unicode);
        PyErr_Print();
        return 1;
    }
    // utf8 belongs to py_unicode, copy it before the release
    output_string.assign(utf8, size);
    Py_DECREF(py_unicode);
    return 0;
}

int convertToUTF8(const std::string &input_string, std::string &output_string)
{
    std::string input_encoding;
    if (detectEncoding(input_string, input_encoding) != 0)
    {
        fprintf(stderr, "convertToUTF8 - cannot detect encoding\n");
        return -1;
    }
    fprintf(stderr, "convertToUTF8 - detected input encoding: %s\n", input_encoding.c_str());
    if (convertToUTF8(input_string, input_encoding, output_string) != 0)
    {
        fprintf(stderr, "convertToUTF8 - cannot convert to utf-8");
        return -1;
    }
    // workaround when uchardet detects wrongly ISO-8859-2 instead
    // of WINDOWS-1250
    if (input_encoding == "ISO-8859-2")
    {
        bool decode_again = false;
        for (int i = 0; i < 10; i++)
        {
            fprintf(stderr, "convertToUTF8 - looking for %#x,%#x: ", iso8859_2_unused_utf8[i][0], iso8859_2_unused_utf8[i][1]);
            void *ptr = memmem(output_string.c_str(), output_string.length(), iso8859_2_unused_utf8[i], 2);
            if (ptr != NULL)
            {
                fprintf(stderr, "found\n");
                decode_again = true;
                break;
            }
            printf("not found\n");
        }
        if (decode_again)
        {
            fprintf(stderr, "convertToUTF8 - ISO-8859-2 is not right encoding, trying WINDOWS-1250\n");
            if (0 != convertToUTF8(input_string, "WINDOWS-1250", output_string))
            {
                fprintf(stderr, "convertToUTF8 - cannot convert to utf-8");
                return -1;
            }
        }
    }
    return 0;
}
#endif

static int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool urlUnquote(const std::string &in, std::string &out, bool plusAsSpace)
{
    // std::string, not a C string: a decoded %00 must stay visible to the
    // caller, which rejects it, instead of silently cutting the value
    std::string result;
    result.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++)
    {
        char c = in[i];
        if (c == '+' && plusAsSpace)
            c = ' ';
        else if (c == '%')
        {
            int high = i + 1 < in.size() ? hexValue(in[i + 1]) : -1;
            int low = i + 2 < in.size() ? hexValue(in[i + 2]) : -1;
            if (high < 0 || low < 0)
                return false;
            c = (char)((high << 4) | low);
            i += 2;
        }
        result += c;
    }
    out = result;
    return true;
}

// a name or value of the fragment as the user wrote it; a stray '%' (no
// escape) keeps that one text as it is instead of failing all headers
static std::string headerUnquote(const std::string &in)
{
    std::string out;
    if (urlUnquote(in, out, true))
        return out;
    fprintf(stderr, "getHeaders - cannot unquote '%s', taking it as it is\n", in.c_str());
    return in;
}

HeaderMap getHeaders(const std::string& url)
{
    std::map<std::string, std::string> headers;
    size_t pos = url.find('#');
    if (pos != std::string::npos && (url.compare(0, 4, "http") == 0 || url.compare(0, 4, "rtsp") == 0))
    {
        std::string headers_str = url.substr(pos + 1);
        // split first, decode each name and value on its own: decoding the
        // whole fragment first turned an encoded "%26" or "%3D" inside a
        // value into a separator
        size_t start = 0;
        while (start <= headers_str.size())
        {
            size_t end = headers_str.find('&', start);
            std::string pair = headers_str.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = end == std::string::npos ? headers_str.size() + 1 : end + 1;
            size_t eq = pair.find('=');
            if (eq == std::string::npos)
                continue;
            std::string name = headerUnquote(pair.substr(0, eq));
            std::string value = headerUnquote(pair.substr(eq + 1));
            // CR/LF would start a header of its own in the HTTP request of
            // the HLS explorer and in the player's header argument, NUL cuts
            // it: such a header is dropped, not repaired
            static const std::string forbidden("\r\n\0", 3);
            if (name.find_first_of(forbidden) != std::string::npos
                    || value.find_first_of(forbidden) != std::string::npos)
            {
                fprintf(stderr, "getHeaders - dropping header with CR/LF/NUL: '%s'\n", pair.c_str());
                continue;
            }
            if (!name.empty() && !value.empty())
            {
                headers[name] = value;
            }
        }
    }
    return headers;
}

