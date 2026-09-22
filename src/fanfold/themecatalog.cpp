#include "themecatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace {

constexpr qint64 maximumCatalogBytes = 4 * 1024 * 1024;
constexpr qint64 maximumManifestBytes = 1024 * 1024;
constexpr qint64 maximumNoticeBytes = 512 * 1024;

/** A bounded regular-file read used for every untrusted bundle member.
 * @param path Exact file to read.
 * @param maximum Maximum accepted byte size.
 * @param label Name used in a refusal message.
 * @param output Receives bytes only after all filesystem checks pass.
 * @param error Receives a user-facing refusal.
 * @return true on a complete read.
 */
bool readRegularFile(const QString &path, qint64 maximum, const QString &label,
                     QByteArray *output, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink()) {
        *error = QStringLiteral("Local theme bundle requires a regular %1").arg(label);
        return false;
    }
    if (info.size() <= 0 || info.size() > maximum) {
        *error = QStringLiteral("Local theme bundle %1 has an invalid size").arg(label);
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Local theme bundle %1 cannot be read").arg(label);
        return false;
    }
    const QByteArray bytes = file.read(maximum + 1);
    if (bytes.size() != info.size()) {
        *error = QStringLiteral("Local theme bundle %1 changed while being read").arg(label);
        return false;
    }
    *output = bytes;
    return true;
}

/** Parse one bounded JSON object without accepting trailing/non-object structures. */
bool parseObject(const QByteArray &bytes, const QString &label, QJsonObject *object,
                 QString *error)
{
    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("Local theme bundle %1 is not valid JSON object data")
                     .arg(label);
        return false;
    }
    *object = document.object();
    return true;
}

/** True for catalog identifiers that can safely form an in-memory selection key. */
bool validId(const QString &value)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return expression.match(value).hasMatch();
}

/** True for the catalog's strict six-digit RGB form. */
bool validColor(const QString &value)
{
    static const QRegularExpression expression(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return expression.match(value).hasMatch();
}

/** True for a bounded, non-whitespace display or provenance field. */
bool validText(const QString &value, qsizetype maximum = 512)
{
    return !value.trimmed().isEmpty() && value == value.trimmed() && value.size() <= maximum;
}

/** Exact non-negative JSON integer, rejecting strings and fractional values. */
int jsonInteger(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return -1;
    }
    const double number = value.toDouble(-1);
    const int integer = int(number);
    return number >= 0 && number == integer ? integer : -1;
}

/** Build one stable key for a validated local family/variant pair. */
QString localKey(const QString &family, const QString &variant)
{
    return QStringLiteral("local:%1/%2").arg(family, variant);
}

} // namespace

ThemeCatalog::ThemeCatalog(QString bundleRoot, QObject *parent)
    : QObject(parent)
{
    const QVariantMap fallback = builtInPalette();
    m_palettes.insert(builtInKey(), fallback);
    m_choices.append(QVariantMap{{QStringLiteral("key"), builtInKey()},
                                 {QStringLiteral("label"),
                                  fallback.value(QStringLiteral("label"))},
                                 {QStringLiteral("group"), QStringLiteral("Built in")},
                                 {QStringLiteral("local"), false}});

    if (!bundleRoot.isEmpty()) {
        loadBundle(QDir::cleanPath(std::move(bundleRoot)));
    }
}

QString ThemeCatalog::builtInKey()
{
    return QStringLiteral("builtin:fan-fold");
}

QVariantMap ThemeCatalog::builtInPalette()
{
    static const QStringList colors{QStringLiteral("#f5f0e6"), QStringLiteral("#efe3cf"),
                                    QStringLiteral("#e6eadf"), QStringLiteral("#dfe7ee"),
                                    QStringLiteral("#ece0e8"), QStringLiteral("#e9e4da"),
                                    QStringLiteral("#2f3136"), QStringLiteral("#3a3327")};
    QVariantList swatches;
    QVariantList tokens;
    for (qsizetype index = 0; index < colors.size(); ++index) {
        const QString &color = colors.at(index);
        swatches.append(color);
        tokens.append(QVariantMap{{QStringLiteral("group"), QStringLiteral("builtin")},
                                  {QStringLiteral("role"),
                                   QStringLiteral("swatch-%1").arg(index + 1)},
                                  {QStringLiteral("hex"), color}});
    }
    return {{QStringLiteral("key"), builtInKey()},
            {QStringLiteral("label"), QStringLiteral("Fan Fold")},
            {QStringLiteral("group"), QStringLiteral("Built in")},
            {QStringLiteral("swatches"), swatches},
            {QStringLiteral("tokens"), tokens},
            {QStringLiteral("background"), colors.first()},
            {QStringLiteral("foreground"), QStringLiteral("#24262a")},
            {QStringLiteral("local"), false}};
}

QVariantMap ThemeCatalog::palette(const QString &key) const
{
    return m_palettes.value(key, m_palettes.value(builtInKey())).toMap();
}

void ThemeCatalog::loadBundle(const QString &bundleRoot)
{
    const QFileInfo rootInfo(bundleRoot);
    if (!QDir::isAbsolutePath(bundleRoot) || !rootInfo.exists() || !rootInfo.isDir()
        || rootInfo.isSymLink()) {
        m_notice = QStringLiteral("Local theme bundle root must be an absolute real directory");
        return;
    }

    const QDir root(bundleRoot);
    QByteArray catalogBytes;
    QByteArray manifestBytes;
    QByteArray noticeBytes;
    if (!readRegularFile(root.filePath(QStringLiteral("catalog.json")), maximumCatalogBytes,
                         QStringLiteral("catalog.json"), &catalogBytes, &m_notice)
        || !readRegularFile(root.filePath(QStringLiteral("manifest.json")), maximumManifestBytes,
                            QStringLiteral("manifest.json"), &manifestBytes, &m_notice)
        || !readRegularFile(root.filePath(QStringLiteral("NOTICES.md")), maximumNoticeBytes,
                            QStringLiteral("NOTICES.md"), &noticeBytes, &m_notice)) {
        return;
    }

    QJsonObject catalogRoot;
    QJsonObject manifest;
    if (!parseObject(catalogBytes, QStringLiteral("catalog.json"), &catalogRoot, &m_notice)
        || !parseObject(manifestBytes, QStringLiteral("manifest.json"), &manifest, &m_notice)) {
        return;
    }

    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(catalogBytes, QCryptographicHash::Sha256).toHex());
    if (manifest.value(QStringLiteral("catalog_sha256")).toString() != digest) {
        m_notice = QStringLiteral("Local theme catalog checksum does not match its manifest");
        return;
    }

    const QString source = manifest.value(QStringLiteral("source")).toString();
    const QString retrieved = manifest.value(QStringLiteral("retrieved")).toString();
    const QJsonObject licensing = manifest.value(QStringLiteral("licensing")).toObject();
    const QString licenseStatus = licensing.value(QStringLiteral("status")).toString();
    const QString licenseUse = licensing.value(QStringLiteral("use")).toString();
    const QString licenseNotice = licensing.value(QStringLiteral("notice")).toString();
    if (!validText(source) || !validText(retrieved) || !validText(licenseStatus)
        || !validText(licenseUse, 2048) || !validText(licenseNotice, 2048)) {
        m_notice = QStringLiteral("Local theme catalog provenance is incomplete");
        return;
    }

    const QJsonObject families = catalogRoot.value(QStringLiteral("families")).toObject();
    if (families.isEmpty()) {
        m_notice = QStringLiteral("Local theme catalog has no families");
        return;
    }

    QVariantList pendingChoices;
    QVariantMap pendingPalettes;
    QSet<QString> uniqueColors;
    int variantsSeen = 0;
    int rolesSeen = 0;
    int declaredSeen = 0;
    QString error;

    for (auto familyIt = families.constBegin(); familyIt != families.constEnd() && error.isEmpty();
         ++familyIt) {
        const QString familyId = familyIt.key();
        const QJsonObject family = familyIt.value().toObject();
        const QString familyName = family.value(QStringLiteral("name")).toString();
        const QJsonObject variants = family.value(QStringLiteral("variants")).toObject();
        const int declared = jsonInteger(family.value(QStringLiteral("declared_variants")));
        if (!validId(familyId) || family.value(QStringLiteral("id")).toString() != familyId
            || !validText(familyName, 128) || !validText(family.value(QStringLiteral("page")).toString())
            || variants.isEmpty() || declared != variants.size()) {
            error = QStringLiteral("family %1 is incomplete or its declared variant count differs")
                        .arg(familyId);
            break;
        }
        declaredSeen += declared;

        for (auto variantIt = variants.constBegin(); variantIt != variants.constEnd(); ++variantIt) {
            const QString variantId = variantIt.key();
            const QJsonObject variant = variantIt.value().toObject();
            const QString variantName = variant.value(QStringLiteral("name")).toString();
            const QString background = variant.value(QStringLiteral("background")).toString().toLower();
            const QString foreground = variant.value(QStringLiteral("foreground")).toString().toLower();
            const QJsonObject roles = variant.value(QStringLiteral("roles")).toObject();
            const QString variantSource = variant.value(QStringLiteral("source")).toString();
            const QString variantDigest = variant.value(QStringLiteral("sha256")).toString();
            if (!validId(variantId) || variant.value(QStringLiteral("id")).toString() != variantId
                || !validText(variantName, 128) || !validColor(background) || !validColor(foreground)
                || roles.isEmpty() || !validText(variant.value(QStringLiteral("page")).toString())
                || !validText(variantSource) || variantDigest.size() != 64
                || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                        .match(variantDigest).hasMatch()
                || variant.value(QStringLiteral("link_verified")).toBool(false) != true) {
                error = QStringLiteral("variant %1/%2 has invalid identifiers, color or provenance")
                            .arg(familyId, variantId);
                break;
            }

            QVariantList tokens;
            QVariantList swatches;
            QSet<QString> paletteColors;
            for (auto groupIt = roles.constBegin(); groupIt != roles.constEnd() && error.isEmpty();
                 ++groupIt) {
                const QString group = groupIt.key();
                const QJsonObject namedRoles = groupIt.value().toObject();
                if (!validId(group) || namedRoles.isEmpty()) {
                    error = QStringLiteral("variant %1/%2 has an invalid or empty role group")
                                .arg(familyId, variantId);
                    break;
                }
                for (auto roleIt = namedRoles.constBegin(); roleIt != namedRoles.constEnd();
                     ++roleIt) {
                    const QString role = roleIt.key();
                    const QString color = roleIt.value().toString().toLower();
                    if (!validId(role) || !validColor(color)) {
                        error = QStringLiteral("variant %1/%2 has an invalid named role color")
                                    .arg(familyId, variantId);
                        break;
                    }
                    tokens.append(QVariantMap{{QStringLiteral("group"), group},
                                              {QStringLiteral("role"), role},
                                              {QStringLiteral("hex"), color}});
                    if (!paletteColors.contains(color)) {
                        paletteColors.insert(color);
                        swatches.append(color);
                    }
                    uniqueColors.insert(color);
                    ++rolesSeen;
                }
            }
            if (!error.isEmpty()) {
                break;
            }
            const QJsonObject primary = roles.value(QStringLiteral("primary")).toObject();
            if (primary.value(QStringLiteral("background")).toString().toLower() != background
                || primary.value(QStringLiteral("foreground")).toString().toLower() != foreground) {
                error = QStringLiteral("variant %1/%2 primary roles disagree with its colors")
                            .arg(familyId, variantId);
                break;
            }

            const QString key = localKey(familyId, variantId);
            const QString label = QStringLiteral("%1 — %2").arg(familyName, variantName);
            const QVariantMap record{{QStringLiteral("key"), key},
                                     {QStringLiteral("label"), label},
                                     {QStringLiteral("group"), familyName},
                                     {QStringLiteral("family"), familyId},
                                     {QStringLiteral("variant"), variantId},
                                     {QStringLiteral("background"), background},
                                     {QStringLiteral("foreground"), foreground},
                                     {QStringLiteral("swatches"), swatches},
                                     {QStringLiteral("tokens"), tokens},
                                     {QStringLiteral("source"), variantSource},
                                     {QStringLiteral("sha256"), variantDigest},
                                     {QStringLiteral("local"), true}};
            pendingPalettes.insert(key, record);
            pendingChoices.append(QVariantMap{{QStringLiteral("key"), key},
                                               {QStringLiteral("label"), label},
                                               {QStringLiteral("group"), familyName},
                                               {QStringLiteral("local"), true}});
            ++variantsSeen;
        }
    }

    if (!error.isEmpty()) {
        m_notice = QStringLiteral("Local theme catalog rejected: %1").arg(error);
        return;
    }

    const QJsonObject inventory = manifest.value(QStringLiteral("inventory")).toObject();
    if (jsonInteger(inventory.value(QStringLiteral("families"))) != families.size()
        || jsonInteger(inventory.value(QStringLiteral("variants_declared"))) != declaredSeen
        || jsonInteger(inventory.value(QStringLiteral("variants_imported"))) != variantsSeen
        || jsonInteger(inventory.value(QStringLiteral("unique_exported_hexes")))
               != uniqueColors.size()) {
        m_notice = QStringLiteral("Local theme catalog inventory does not match its complete data");
        return;
    }

    for (auto it = pendingPalettes.constBegin(); it != pendingPalettes.constEnd(); ++it) {
        m_palettes.insert(it.key(), it.value());
    }
    m_choices.append(pendingChoices);
    m_provenance = {{QStringLiteral("source"), source},
                    {QStringLiteral("retrieved"), retrieved},
                    {QStringLiteral("status"), licenseStatus},
                    {QStringLiteral("use"), licenseUse},
                    {QStringLiteral("notice"), licenseNotice},
                    {QStringLiteral("catalogSha256"), digest},
                    {QStringLiteral("noticesSha256"),
                     QString::fromLatin1(QCryptographicHash::hash(noticeBytes,
                                                                  QCryptographicHash::Sha256)
                                             .toHex())}};
    m_familyCount = static_cast<int>(families.size());
    m_variantCount = variantsSeen;
    m_roleCount = rolesSeen;
    m_uniqueColorCount = static_cast<int>(uniqueColors.size());
    m_localCatalogAvailable = true;
    m_notice.clear();
}
