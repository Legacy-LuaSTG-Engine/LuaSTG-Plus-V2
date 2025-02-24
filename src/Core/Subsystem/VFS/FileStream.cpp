/**
 * @file
 * @date 2022/2/20
 * @author 9chu
 * 此文件为 LuaSTGPlus 项目的一部分，版权与许可声明详见 COPYRIGHT.txt。
 */
#include <lstg/Core/Subsystem/VFS/FileStream.hpp>

#ifndef LSTG_PLATFORM_WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef LSTG_PLATFORM_APPLE
#define off64_t off_t
#define ftello64 ftello
#define fseeko64 fseeko
#define stat64 stat
#define fstat64 fstat
#endif
#else
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <io.h>
#define off64_t __int64
#define ftello64 _ftelli64
#define fseeko64 _fseeki64
#endif

using namespace std;
using namespace lstg;
using namespace lstg::Subsystem::VFS;

#ifdef LSTG_PLATFORM_WIN32
namespace
{
    wstring Utf8StringToWideString(const std::string& str)
    {
        wstring ret;
        auto length = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), 0, 0);
        ret.resize(length);
        ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), ret.data(), length);
        return ret;
    }
}
#endif

FileStream::FileStream(std::filesystem::path path, FileAccessMode access, FileOpenFlags openFlags)
    : m_stPath(std::move(path)), m_iAccess(access), m_iFlags(openFlags)
{
#ifdef LSTG_PLATFORM_WIN32
#define U(x) L##x
    const wchar_t* mode = nullptr;
#else
#define U(x) x
    const char* mode = nullptr;
#endif

    // 决定打开的模式
    switch (access)
    {
        default:
            assert(false);
            [[fallthrough]];
        case FileAccessMode::Read:
            mode = U("rb");
            break;
        case FileAccessMode::Write:
            if (openFlags & FileOpenFlags::Truncate)
                mode = U("wb");
            else
                mode = U("r+b");  // a 模式下不能 seek，用 r+ 代替
            break;
        case FileAccessMode::ReadWrite:
            if (openFlags & FileOpenFlags::Truncate)
                mode = U("w+b");
            else
                mode = U("r+b");
            break;
    }

#undef U

    // 打开文件
    // FIXME: 考虑用 open / CreateFile 代替
#ifdef LSTG_PLATFORM_WIN32
    auto wpath = Utf8StringToWideString(m_stPath.string());
    m_pHandle = ::_wfopen(m_stPath.c_str(), mode);
    if (!m_pHandle)
        throw system_error(error_code(errno, generic_category()));
#else
    m_pHandle = ::fopen(m_stPath.c_str(), mode);
    if (!m_pHandle)
        throw system_error(error_code(errno, generic_category()));
#endif
}

FileStream::~FileStream()
{
    if (m_pHandle)
    {
        ::fclose(m_pHandle);
        m_pHandle = nullptr;
    }
}

FileStream::FileStream(FileStream&& rhs) noexcept
    : m_stPath(std::move(rhs.m_stPath)), m_iAccess(rhs.m_iAccess), m_iFlags(rhs.m_iFlags), m_pHandle(rhs.m_pHandle)
{
    rhs.m_iAccess = FileAccessMode::Read;
    rhs.m_iFlags = FileOpenFlags::None;
    rhs.m_pHandle = nullptr;
}


FileStream& FileStream::operator=(FileStream&& rhs) noexcept
{
    if (m_pHandle)
    {
        ::fclose(m_pHandle);
        m_pHandle = nullptr;
    }

    m_stPath = std::move(rhs.m_stPath);
    m_iAccess = rhs.m_iAccess;
    m_iFlags = rhs.m_iFlags;
    m_pHandle = rhs.m_pHandle;

    rhs.m_pHandle = nullptr;
    rhs.m_iAccess = FileAccessMode::Read;
    rhs.m_iFlags = FileOpenFlags::None;
    return *this;
}

bool FileStream::IsReadable() const noexcept
{
    return m_iAccess == FileAccessMode::Read || m_iAccess == FileAccessMode::ReadWrite;
}

bool FileStream::IsWriteable() const noexcept
{
    return m_iAccess == FileAccessMode::Write || m_iAccess == FileAccessMode::ReadWrite;
}

bool FileStream::IsSeekable() const noexcept
{
    return true;
}

Result<uint64_t> FileStream::GetLength() const noexcept
{
    assert(m_pHandle);

#ifndef LSTG_PLATFORM_WIN32
    struct ::stat64 buf {};
    if (-1 == ::fstat64(::fileno(m_pHandle), &buf))
        return error_code(errno, generic_category());
    return static_cast<size_t>(buf.st_size);
#else
    auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(m_pHandle)));
    if (handle == INVALID_HANDLE_VALUE)
        return error_code(errno, generic_category());
    LARGE_INTEGER largeInteger{};
    if (FALSE == ::GetFileSizeEx(handle, &largeInteger))
        return error_code(::GetLastError(), system_category());
    return static_cast<size_t>(largeInteger.QuadPart);
#endif
}

Result<void> FileStream::SetLength(uint64_t length) noexcept
{
    assert(m_pHandle);

#ifndef LSTG_PLATFORM_WIN32
    auto ret = ::ftruncate(::fileno(m_pHandle), length);
    if (ret == -1)
        return error_code(errno, generic_category());
    return Seek(0, StreamSeekOrigins::End).GetError();
#else
    auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(m_pHandle)));
    if (handle == INVALID_HANDLE_VALUE)
        return error_code(errno, generic_category());

    LARGE_INTEGER largeInteger;
    largeInteger.QuadPart = length;
    if (INVALID_SET_FILE_POINTER == ::SetFilePointer(handle, largeInteger.LowPart, &largeInteger.HighPart, FILE_BEGIN))
        return error_code(::GetLastError(), system_category());
    if (FALSE == ::SetEndOfFile(handle))
        return error_code(::GetLastError(), system_category());
    return {};
#endif
}

Result<uint64_t> FileStream::GetPosition() const noexcept
{
    assert(m_pHandle);

    auto offset = ::ftello64(m_pHandle);
    if (offset == -1)
        return error_code(errno, generic_category());
    return static_cast<size_t>(offset);
}

Result<void> FileStream::Seek(int64_t offset, StreamSeekOrigins origin) noexcept
{
    assert(m_pHandle);

    off64_t off = 0;
    switch (origin)
    {
        case StreamSeekOrigins::Begin:
            off = ::fseeko64(m_pHandle, offset, SEEK_SET);
            break;
        case StreamSeekOrigins::Current:
            off = ::fseeko64(m_pHandle, offset, SEEK_CUR);
            break;
        case StreamSeekOrigins::End:
            off = ::fseeko64(m_pHandle, offset, SEEK_END);
            break;
        default:
            assert(false);
            break;
    }
    if (off < 0)
        return error_code(errno, generic_category());
    return {};
}

Result<bool> FileStream::IsEof() const noexcept
{
    assert(m_pHandle);

    auto ret = ::feof(m_pHandle);
    if (ret < 0)
        return error_code(errno, generic_category());
    return ret > 0;
}

Result<void> FileStream::Flush() noexcept
{
    assert(m_pHandle);

    if (0 != ::fflush(m_pHandle))
        return error_code(errno, generic_category());

#ifndef LSTG_PLATFORM_WIN32
    if (-1 == ::fsync(::fileno(m_pHandle)))
        return error_code(errno, generic_category());
#endif

    return {};
}

Result<size_t> FileStream::Read(uint8_t* buffer, size_t length) noexcept
{
    assert(m_pHandle);

    if (length == 0)
    {
        if (::ferror(m_pHandle))
            return make_error_code(errc::io_error);
        return static_cast<size_t>(0u);
    }

    auto ret = ::fread(buffer, 1, length, m_pHandle);
    if (ret <= 0)
    {
        if (::ferror(m_pHandle))
            return error_code(errno, generic_category());
        assert(::feof(m_pHandle));
        return static_cast<size_t>(0);
    }
    return ret;
}

Result<void> FileStream::Write(const uint8_t* buffer, size_t length) noexcept
{
    assert(m_pHandle);

    auto ret = ::fwrite(buffer, 1, length, m_pHandle);
    if (ret != length)
        return error_code(errno, generic_category());
    return {};
}

Result<StreamPtr> FileStream::Clone() const noexcept
{
    assert(m_pHandle);

    // 获取当前的读写位置
    auto pos = GetPosition();
    if (!pos)
        return pos.GetError();

    try
    {
        // Clone 的时候不会 trunc
        auto flag = m_iFlags;
        flag ^= FileOpenFlags::Truncate;

        auto stream = std::make_shared<FileStream>(m_stPath, m_iAccess, flag);

        // 设置读写位置
        auto err = stream->Seek(*pos, StreamSeekOrigins::Begin);
        if (!err)
            return pos.GetError();

        return stream;
    }
    catch (const system_error& ex)
    {
        return ex.code();
    }
    catch (const bad_alloc& ex)
    {
        return make_error_code(errc::not_enough_memory);
    }
    catch (...)
    {
        return make_error_code(errc::io_error);
    }
}
