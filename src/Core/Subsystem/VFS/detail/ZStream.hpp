/**
 * @file
 * @date 2022/2/28
 * @author 9chu
 * 这个文件是 LuaSTGPlus 项目的一部分，请在项目所定义之授权许可范围内合规使用。
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <zlib-ng.h>

namespace lstg::Subsystem::VFS::detail
{
    struct DeflateInitTag {};
    struct InflateInitTag {};

    class ZStream
    {
    public:
        ZStream(DeflateInitTag, int compressionLevel = Z_DEFAULT_COMPRESSION);
        ZStream(InflateInitTag, bool rawDeflateData = false);

        ZStream(ZStream&&) = delete;
        ZStream& operator=(ZStream&&) = delete;

        ~ZStream();

    public:
        const ::zng_stream* operator*() const noexcept { return &m_stZStream; }
        ::zng_stream* operator*() noexcept { return &m_stZStream; }

        const ::zng_stream* operator->() const noexcept { return &m_stZStream; }
        ::zng_stream* operator->() noexcept { return &m_stZStream; }

    private:
        bool m_bDeflateStream = false;
        ::zng_stream m_stZStream{};
    };
}
