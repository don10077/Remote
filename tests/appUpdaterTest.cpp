#include "../app/src/appUpdater.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
using namespace harmony::app;

/// Ответ выпуска с реальными формами URL и SHA-256 GitHub.
QJsonObject release(const QString& tag) {
    const QString number = tag.mid(1);
    const QString name = "Harmony-" + number + "-setup.exe";
    return {{"tag_name", tag}, {"draft", false}, {"prerelease", true},
        {"html_url", "https://github.com/don10077/Remote/releases/tag/" + tag},
        {"assets", QJsonArray{QJsonObject{{"name", name}, {"size", 7},
            {"digest", "sha256:" + QString::fromLatin1(QCryptographicHash::hash("payload", QCryptographicHash::Sha256).toHex())},
            {"browser_download_url", "https://github.com/don10077/Remote/releases/download/" + tag + '/' + name}}}}};
}

QByteArray response(const QJsonArray& values) { return QJsonDocument(values).toJson(); }

TEST(AppUpdater, ChoosesNewestNumericReleaseIncludingProjectPreReleases) {
    const auto update = findUpdate(response({release("v0.8.0"), release("v0.10.0"), release("v0.9.0")}), "0.7.0");
    ASSERT_TRUE(update);
    EXPECT_EQ(update->version, "0.10.0");
    EXPECT_FALSE(update->installer.isEmpty());
    EXPECT_EQ(update->sha256.size(), 32);
}

TEST(AppUpdater, IgnoresDraftsOldVersionsMalformedTagsAndMalformedResponses) {
    auto draft = release("v9.0.0"); draft.insert("draft", true);
    EXPECT_FALSE(findUpdate(response({draft, release("v0.6.0"), release("v0.7.0"), release("v1.0.0-rc1")}), "0.7.0"));
    EXPECT_FALSE(findUpdate("garbage", "0.7.0"));
    EXPECT_FALSE(findUpdate(response({release("v1.0.0")}), "invalid"));
}

TEST(AppUpdater, NeverInstallsAnAssetFromAnotherRepositoryOrWithoutItsDigest) {
    for (const auto& url : {"http://github.com/don10077/Remote/releases/download/v0.8.0/Harmony-0.8.0-setup.exe",
                           "https://github.com/another/Remote/releases/download/v0.8.0/Harmony-0.8.0-setup.exe",
                           "https://attacker.invalid/Harmony-0.8.0-setup.exe"}) {
        auto item = release("v0.8.0");
        auto asset = item.value("assets").toArray().first().toObject();
        asset.insert("browser_download_url", url);
        item.insert("assets", QJsonArray{asset});
        const auto update = findUpdate(response({item}), "0.7.0");
        ASSERT_TRUE(update);
        EXPECT_TRUE(update->installer.isEmpty()) << url;
    }
    auto item = release("v0.8.0");
    auto asset = item.value("assets").toArray().first().toObject();
    asset.remove("digest");
    item.insert("assets", QJsonArray{asset});
    ASSERT_TRUE(findUpdate(response({item}), "0.7.0"));
    EXPECT_TRUE(findUpdate(response({item}), "0.7.0")->installer.isEmpty());
    item.insert("html_url", "https://attacker.invalid/releases/tag/v0.8.0");
    EXPECT_FALSE(findUpdate(response({item}), "0.7.0"));
}

TEST(AppUpdater, RejectsTruncatedAndTamperedInstallers) {
    const auto update = findUpdate(response({release("v0.8.0")}), "0.7.0");
    ASSERT_TRUE(update);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("setup.exe");
    for (const QByteArray& bytes : {QByteArray("pay"), QByteArray("tamper!"), QByteArray("payload")}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
        file.close();
        EXPECT_EQ(verifyUpdate(path, *update), bytes == "payload");
    }
}
} // namespace
