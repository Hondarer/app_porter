#include <testfw.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include <com_util/base/platform.h>
#include <porter/porter_spec.h>

// libporter が公開エクスポートすべき関数の一覧。
// 関数の追加・削除は、このテーブルのみを編集する。
// 名前一致チェックとシグネチャの static_assert は、いずれも本テーブルから生成する。
#define POTR_EXPORT_FUNCTION_TABLE(EXPORT_ENTRY) \
    EXPORT_ENTRY(potrOpenService, int(POTR_API *)(const PotrGlobalConfig *global, const PotrServiceDef *service, \
                                                  PotrRole role, PotrRecvCallback callback, PotrContext **handle)) \
    EXPORT_ENTRY(potrOpenServiceFromConfig, \
                 int(POTR_API *)(const char *config_path, int64_t service_id, PotrRole role, \
                                 PotrRecvCallback callback, PotrContext **handle)) \
    EXPORT_ENTRY(potrSend, \
                 int(POTR_API *)(PotrContext * handle, PotrPeerId peer_id, const void *data, size_t len, int flags)) \
    EXPORT_ENTRY(potrDisconnectPeer, int(POTR_API *)(PotrContext * handle, PotrPeerId peer_id)) \
    EXPORT_ENTRY(potrCloseService, int(POTR_API *)(PotrContext * handle)) \
    EXPORT_ENTRY(potrGetTracer, com_util_tracer *(POTR_API *)(void)) \
    EXPORT_ENTRY(potrGetServiceType, int(POTR_API *)(const char *config_path, int64_t service_id, PotrType *type))

// libporter が公開エクスポートすべき変数の一覧。
// 現時点ではエントリなし (公開ヘッダーに dllexport 付きの変数エクスポートが存在しないため) 。
// 公開ヘッダーへ変数エクスポートを追加する場合は、ここへ X(変数名, 型 *) の形で登録する。
#define POTR_EXPORT_VARIABLE_TABLE(EXPORT_ENTRY)

#define POTR_EXPORT_TABLE(EXPORT_ENTRY) \
    POTR_EXPORT_FUNCTION_TABLE(EXPORT_ENTRY) \
    POTR_EXPORT_VARIABLE_TABLE(EXPORT_ENTRY)

// テーブルからシグネチャの static_assert と期待シンボル名一覧を生成する。
POTR_EXPORT_TABLE(TESTFW_EXPORT_STATIC_ASSERT_ENTRY)

static const char *const kExpectedExportNames[] = {POTR_EXPORT_TABLE(TESTFW_EXPORT_NAME_ENTRY)};

static const std::map<std::string, std::string> kExpectedExportSignatures = {
    POTR_EXPORT_TABLE(TESTFW_EXPORT_SIGNATURE_ENTRY)};

class exportTest : public Test
{
  protected:
    std::string workspace_root;
    std::string dll_path;

    void SetUp() override
    {
        workspace_root = findWorkspaceRoot();
        ASSERT_FALSE(workspace_root.empty()) << "ワークスペースルートが見つかりません";
        dll_path = workspace_root + "/app/porter/prod/lib/libporter" TESTFW_SHARED_LIBRARY_EXTENSION;
    }
};

// libporter のエクスポート シンボル名に不足や想定外がないことの確認
TEST_F(exportTest, symbol_names_match)
{
    // Arrange
    std::set<std::string> expected(
        std::begin(kExpectedExportNames),
        std::end(kExpectedExportNames)); // [状態] - POTR_EXPORT_TABLE から期待シンボル名一覧を構築する。
#if defined(PLATFORM_WINDOWS)
    // _ident_manifest_libporter_dll は gen_ident_manifest.py が自動生成するビルド識別データであり、
    // 関数ではないためシグネチャ検証の対象外としつつ、名前一致の期待値には含める。
    expected.insert(testing::identManifestSymbolName(
        "libporter" TESTFW_SHARED_LIBRARY_EXTENSION)); // [状態] - IDENT manifest シンボル名を期待値へ追加する (Windows のみ実際にエクスポートされる) 。
#endif                                                 /* PLATFORM_WINDOWS */

    // Pre-Assert

    // Act
    std::set<std::string> actual = testing::getActualExportNames(
        dll_path); // [手順] - dumpbin/nm で libporter の実際のエクスポート一覧を取得する。

    // Assert
    testing::expectExportNamesMatch(
        expected, actual,
        kExpectedExportSignatures); // [確認_正常系] - 期待シンボルとの不足や想定外がないこと (Windows / Linux とも完全一致) 。
}

// 公開ヘッダーの変数宣言が dllexport マクロ (POTR_EXPORT) を
// 伴わずに追加されていないことの確認
TEST_F(exportTest, public_header_variables_declare_export_macro)
{
    // Arrange
    std::string include_dir =
        workspace_root +
        "/app/porter/prod/include"; // [状態] - 公開ヘッダーのディレクトリを "/app/porter/prod/include" に設定する。

    // Pre-Assert

    // Act
    std::vector<std::string> undecorated = testing::findUndecoratedExternVariables(
        include_dir,
        "POTR_EXPORT"); // [手順] - prod/include 配下を走査し、POTR_EXPORT を伴わない extern 変数宣言を集める。

    // Assert
    EXPECT_TRUE(undecorated.empty()) << "POTR_EXPORT を伴わない変数宣言: "
                                     << testing::joinNames(
                                            undecorated); // [確認_正常系] - 該当する宣言が 1 件もないこと。
}
