#ifndef DOCUMENTSETTINGSDIALOG_H
#define DOCUMENTSETTINGSDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

// Forward declarations
class MainWindow;
class Document;

/**
 * @brief Per-document override settings dialog ("Current Document Settings").
 *
 * Unlike ControlPanelDialog (which edits global QSettings defaults for NEW
 * documents), this dialog applies overrides directly to the currently open
 * Document - a "live" apply. It replaces the standalone OCR Language item in
 * the navigation-bar overflow menu.
 *
 * This first iteration implements:
 * - Page tab: page-size override (sets Document::defaultPageSize; new pages
 *   added via Ctrl+Shift+A use it - existing pages are NOT resized).
 * - Language tab: per-document OCR recognizer language override.
 *
 * Tools (CJK grid-cell mode) and Theme (PDF dark inversion) tabs are planned as
 * a follow-up; createToolsTab()/createThemeTab() hooks are intentionally left
 * for that work.
 */
class DocumentSettingsDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Construct the per-document settings dialog.
     * @param mainWindow Reference to MainWindow (OCR data + worker refresh).
     * @param doc The currently open document to override (may be nullptr).
     * @param parent Parent widget.
     */
    explicit DocumentSettingsDialog(MainWindow* mainWindow, Document* doc,
                                    QWidget* parent = nullptr);

protected:
    /**
     * @brief Override done() to fix Android/iOS keyboard crash (BUG-A001),
     * mirroring ControlPanelDialog::done().
     */
    void done(int result) override;

private slots:
    void applyChanges();
    void onPageSizePresetChanged(int index);

private:
    MainWindow* mainWindowRef = nullptr;
    Document* m_doc = nullptr;

    // === Dialog widgets ===
    QTabWidget* tabWidget = nullptr;
    QPushButton* applyButton = nullptr;
    QPushButton* okButton = nullptr;
    QPushButton* cancelButton = nullptr;

    // === Page tab ===
    QWidget* pageTab = nullptr;
    QComboBox* pageSizeCombo = nullptr;
    QLabel* pageSizeDimLabel = nullptr;
    void createPageTab();

    // === Language tab ===
    QWidget* languageTab = nullptr;
    QComboBox* ocrLanguageCombo = nullptr;
    void createLanguageTab();

    // === Future override tabs (follow-up plan) ===
    // void createToolsTab();   // CJK grid-cell mode
    // void createThemeTab();   // PDF dark inversion / full-page invert

    void loadSettings();
};

#endif // DOCUMENTSETTINGSDIALOG_H
