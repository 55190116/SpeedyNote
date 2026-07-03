#include "DocumentSettingsDialog.h"
#include "../../MainWindow.h"
#include "../../core/Document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QCompleter>
#include <QLocale>
#include <QSizeF>
#include <algorithm>

// Android/iOS keyboard fix (BUG-A001)
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QTimer>
#endif

DocumentSettingsDialog::DocumentSettingsDialog(MainWindow* mainWindow, Document* doc,
                                               QWidget* parent)
    : QDialog(parent), mainWindowRef(mainWindow), m_doc(doc) {

    setWindowTitle(tr("Current Document Settings"));
    resize(450, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Document name header (mirrors the old OCR Language dialog).
    if (m_doc) {
        QLabel* docLabel = new QLabel(tr("Document: %1").arg(m_doc->name), this);
        docLabel->setStyleSheet("font-weight: bold;");
        mainLayout->addWidget(docLabel);
        mainLayout->addSpacing(4);
    }

    tabWidget = new QTabWidget(this);

    createPageTab();
    createLanguageTab();
    // Follow-up plan: createToolsTab() (CJK grid-cell mode),
    //                 createThemeTab() (PDF dark inversion / full-page invert).

    mainLayout->addWidget(tabWidget);

    // === Buttons ===
    applyButton = new QPushButton(tr("Apply"), this);
    okButton = new QPushButton(tr("OK"), this);
    cancelButton = new QPushButton(tr("Cancel"), this);

    connect(applyButton, &QPushButton::clicked, this, &DocumentSettingsDialog::applyChanges);
    connect(okButton, &QPushButton::clicked, this, [this]() {
        applyChanges();
        accept();
    });
    connect(cancelButton, &QPushButton::clicked, this, &DocumentSettingsDialog::reject);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    // Load current document values into the UI.
    loadSettings();
}

void DocumentSettingsDialog::done(int result)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // BUG-A001 Fix: defer close so Android/iOS keyboard operations complete
    // (mirrors ControlPanelDialog::done()).
    static bool isDeferring = false;
    if (isDeferring) {
        QDialog::done(result);
        return;
    }

    if (QWidget* focused = QApplication::focusWidget()) {
        focused->clearFocus();
    }
    if (QGuiApplication::inputMethod()) {
        QGuiApplication::inputMethod()->hide();
        QGuiApplication::inputMethod()->commit();
    }

    isDeferring = true;
    int savedResult = result;
    QTimer::singleShot(150, this, [this, savedResult]() {
        isDeferring = false;
        QDialog::done(savedResult);
    });
    return;
#else
    QDialog::done(result);
#endif
}

// ============================================================================
// Page tab - page size override
// ============================================================================

void DocumentSettingsDialog::createPageTab()
{
    pageTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(pageTab);

    layout->addSpacing(6);

    QLabel* descLabel = new QLabel(
        tr("These settings override the defaults for THIS document only. "
           "Page size applies to newly added pages; existing pages are not resized."),
        pageTab);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 10px;");
    layout->addWidget(descLabel);

    // Paper size preset (same presets as the base Control Panel).
    QHBoxLayout* pageSizeLayout = new QHBoxLayout();
    QLabel* pageSizeLabel = new QLabel(tr("Paper Size:"), pageTab);
    pageSizeLabel->setMinimumWidth(120);
    pageSizeLayout->addWidget(pageSizeLabel);

    pageSizeCombo = new QComboBox(pageTab);
    // mm to px at 96 DPI: mm * 96 / 25.4
    pageSizeCombo->addItem(tr("A3 (297 × 420 mm)"), QSizeF(1123, 1587));
    pageSizeCombo->addItem(tr("B4 (250 × 353 mm)"), QSizeF(945, 1334));
    pageSizeCombo->addItem(tr("A4 (210 × 297 mm)"), QSizeF(794, 1123));
    pageSizeCombo->addItem(tr("B5 (176 × 250 mm)"), QSizeF(665, 945));
    pageSizeCombo->addItem(tr("A5 (148 × 210 mm)"), QSizeF(559, 794));
    pageSizeCombo->addItem(tr("US Letter (8.5 × 11 in)"), QSizeF(816, 1056));
    pageSizeCombo->addItem(tr("US Legal (8.5 × 14 in)"), QSizeF(816, 1344));
    pageSizeCombo->addItem(tr("US Tabloid (11 × 17 in)"), QSizeF(1056, 1632));

    connect(pageSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DocumentSettingsDialog::onPageSizePresetChanged);
    pageSizeLayout->addWidget(pageSizeCombo, 1);
    layout->addLayout(pageSizeLayout);

    // Dimensions (read-only display).
    QHBoxLayout* pageDimLayout = new QHBoxLayout();
    QLabel* pageDimLabel = new QLabel(tr("Dimensions:"), pageTab);
    pageDimLabel->setMinimumWidth(120);
    pageDimLayout->addWidget(pageDimLabel);

    pageSizeDimLabel = new QLabel(pageTab);
    pageSizeDimLabel->setStyleSheet("color: #666; font-style: italic;");
    pageDimLayout->addWidget(pageSizeDimLabel);
    pageDimLayout->addStretch();
    layout->addLayout(pageDimLayout);

    layout->addStretch();

    if (!m_doc) {
        pageSizeCombo->setEnabled(false);
    }

    tabWidget->addTab(pageTab, tr("Page"));
}

void DocumentSettingsDialog::onPageSizePresetChanged(int index)
{
    if (index < 0 || !pageSizeCombo || !pageSizeDimLabel) return;

    QSizeF size = pageSizeCombo->itemData(index).toSizeF();
    pageSizeDimLabel->setText(tr("%1 × %2 px (at 96 DPI)")
        .arg(static_cast<int>(size.width()))
        .arg(static_cast<int>(size.height())));
}

// ============================================================================
// Language tab - per-document OCR recognizer override
// ============================================================================

void DocumentSettingsDialog::createLanguageTab()
{
    languageTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(languageTab);

    layout->addSpacing(6);

    QLabel* label = new QLabel(
        tr("Handwriting recognition language for this document:"), languageTab);
    label->setWordWrap(true);
    layout->addWidget(label);

    ocrLanguageCombo = new QComboBox(languageTab);
    ocrLanguageCombo->addItem(tr("Use global setting"), QStringLiteral(""));
    ocrLanguageCombo->addItem(tr("Auto-detect (system default)"), QStringLiteral("auto"));

    // Partition languages: common first, then the rest sorted by display name
    // (same behavior as the former MainWindow::showOcrLanguageDialog()).
    static const QStringList commonTags = {
        QStringLiteral("en-US"), QStringLiteral("en-GB"),
        QStringLiteral("zh-Hani-CN"), QStringLiteral("zh-Hani-TW"),
        QStringLiteral("ja"), QStringLiteral("ko"),
        QStringLiteral("es-ES"), QStringLiteral("fr-FR"),
        QStringLiteral("de-DE"), QStringLiteral("pt-BR"),
        QStringLiteral("it-IT"), QStringLiteral("ru"), QStringLiteral("ar"),
    };

    auto displayNameForTag = [](const QString& tag) -> QString {
        QLocale locale(QString(tag).replace(QLatin1Char('-'), QLatin1Char('_')));
        QString name = locale.nativeLanguageName();
        if (name.isEmpty() || name == QLatin1String("C"))
            return tag;
        return QStringLiteral("%1 (%2)").arg(name, tag);
    };

    const QStringList available =
        mainWindowRef ? mainWindowRef->ocrAvailableLanguages() : QStringList();

    QStringList common, rest;
    for (const auto& lang : available) {
        if (commonTags.contains(lang))
            common.append(lang);
        else
            rest.append(lang);
    }

    std::sort(rest.begin(), rest.end(), [&](const QString& a, const QString& b) {
        return displayNameForTag(a).toLower() < displayNameForTag(b).toLower();
    });

    for (const auto& tag : commonTags) {
        if (common.contains(tag))
            ocrLanguageCombo->addItem(displayNameForTag(tag), tag);
    }
    if (!common.isEmpty() && !rest.isEmpty())
        ocrLanguageCombo->insertSeparator(ocrLanguageCombo->count());
    for (const auto& tag : rest)
        ocrLanguageCombo->addItem(displayNameForTag(tag), tag);

    ocrLanguageCombo->setEditable(true);
    ocrLanguageCombo->setInsertPolicy(QComboBox::NoInsert);
    if (ocrLanguageCombo->completer())
        ocrLanguageCombo->completer()->setFilterMode(Qt::MatchContains);

    layout->addWidget(ocrLanguageCombo);

    layout->addSpacing(8);

    QLabel* globalNote = new QLabel(
        tr("\"Use global setting\" inherits from Settings > Language > "
           "Handwriting Recognition Language."),
        languageTab);
    globalNote->setWordWrap(true);
    globalNote->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(globalNote);

    layout->addStretch();

    if (!m_doc) {
        ocrLanguageCombo->setEnabled(false);
    }

    tabWidget->addTab(languageTab, tr("Language"));
}

// ============================================================================
// Load / Apply
// ============================================================================

void DocumentSettingsDialog::loadSettings()
{
    if (!m_doc) return;

    // Page size: select the preset matching the document's default page size.
    if (pageSizeCombo) {
        const QSizeF current = m_doc->defaultPageSize;
        int matchIndex = -1;
        for (int i = 0; i < pageSizeCombo->count(); ++i) {
            QSizeF preset = pageSizeCombo->itemData(i).toSizeF();
            if (qFuzzyCompare(preset.width(), current.width()) &&
                qFuzzyCompare(preset.height(), current.height())) {
                matchIndex = i;
                break;
            }
        }
        if (matchIndex >= 0) {
            pageSizeCombo->setCurrentIndex(matchIndex);
        } else if (current.isValid() && !current.isEmpty()) {
            // No preset matches (e.g. a PDF-imported document with an arbitrary
            // page size). Insert a "Custom" entry holding the real size and
            // select it, so applying the dialog does NOT silently overwrite the
            // document's page size with the first preset.
            pageSizeCombo->insertItem(0,
                tr("Custom (%1 × %2 px)")
                    .arg(static_cast<int>(current.width()))
                    .arg(static_cast<int>(current.height())),
                current);
            pageSizeCombo->setCurrentIndex(0);
        }
        // Ensure the dimensions label reflects the current selection even when
        // the current size is custom (no matching preset).
        onPageSizePresetChanged(pageSizeCombo->currentIndex());
    }

    // OCR language: pre-select the document's per-document override.
    if (ocrLanguageCombo) {
        int idx = ocrLanguageCombo->findData(m_doc->ocrLanguage);
        if (idx >= 0) ocrLanguageCombo->setCurrentIndex(idx);
    }
}

void DocumentSettingsDialog::applyChanges()
{
    if (!m_doc) return;

    // Page size override (option b): update the document default so newly added
    // pages (Ctrl+Shift+A) use it. Existing pages are intentionally NOT resized.
    // Only write (and dirty the document) when the size actually changed, so a
    // user who only edits the OCR language doesn't spuriously mark it modified.
    if (pageSizeCombo) {
        QSizeF selected = pageSizeCombo->currentData().toSizeF();
        if (selected.isValid() && !selected.isEmpty() &&
            (!qFuzzyCompare(selected.width(), m_doc->defaultPageSize.width()) ||
             !qFuzzyCompare(selected.height(), m_doc->defaultPageSize.height()))) {
            m_doc->defaultPageSize = selected;
            m_doc->markModified();
        }
    }

    // OCR language override (marks modified + refreshes the OCR worker). Only
    // apply when it actually changed, so opening the dialog and pressing OK
    // without touching this tab doesn't dirty the document or churn the worker.
    if (ocrLanguageCombo && mainWindowRef) {
        const QString selectedLang = ocrLanguageCombo->currentData().toString();
        if (selectedLang != m_doc->ocrLanguage) {
            mainWindowRef->applyDocumentOcrLanguage(m_doc, selectedLang);
        }
    }
}
