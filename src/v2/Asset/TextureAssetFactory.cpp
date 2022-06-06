/**
 * @file
 * @date 2022/5/30
 * @author 9chu
 * 这个文件是 LuaSTGPlus 项目的一部分，请在项目所定义之授权许可范围内合规使用。
 */
#include <lstg/v2/Asset/TextureAssetFactory.hpp>

#include <memory>
#include <lstg/Core/Subsystem/AssetSystem.hpp>
#include <lstg/Core/Subsystem/Asset/ArgumentHelper.hpp>
#include <lstg/v2/Asset/TextureAsset.hpp>
#include <lstg/v2/Asset/TextureAssetLoader.hpp>

using namespace std;
using namespace lstg;
using namespace lstg::v2::Asset;

std::string_view TextureAssetFactory::GetAssetTypeName() const noexcept
{
    return "Texture";
}

Subsystem::Asset::AssetTypeId TextureAssetFactory::GetAssetTypeId() const noexcept
{
    return TextureAsset::GetAssetTypeIdStatic();
}

Result<Subsystem::Asset::CreateAssetResult> TextureAssetFactory::CreateAsset(Subsystem::AssetSystem& assetSystem,
    Subsystem::Asset::AssetPoolPtr pool, std::string_view name, const nlohmann::json& arguments) noexcept
{
    auto pixelPerUnit = Subsystem::Asset::ReadArgument<double>(arguments, "/ppu", 1.);

    try
    {
        auto basicTexture = assetSystem.CreateAsset<Subsystem::Asset::BasicTextureAsset>(pool, {}, arguments);
        basicTexture.ThrowIfError();
        auto asset = make_shared<TextureAsset>(string{name}, std::move(*basicTexture), pixelPerUnit);
        auto loader = make_shared<TextureAssetLoader>(asset);
        return Subsystem::Asset::CreateAssetResult {
            static_pointer_cast<Subsystem::Asset::Asset>(asset),
            static_pointer_cast<Subsystem::Asset::AssetLoader>(loader)
        };
    }
    catch (const std::system_error& ex)
    {
        return ex.code();
    }
    catch (...)
    {
        return make_error_code(errc::not_enough_memory);
    }
}
