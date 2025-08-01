#include "ControlPanelDialog.h"
#include "MainWindow.h"
#include "ButtonMappingTypes.h"
#include "SDLControllerManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSpacerItem>
#include <QTableWidget>
#include <QPushButton>
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QApplication>
#include <QMetaObject>

ControlPanelDialog::ControlPanelDialog(MainWindow *mainWindow, InkCanvas *targetCanvas, QWidget *parent)
    : QDialog(parent), canvas(targetCanvas), selectedColor(canvas->getBackgroundColor()), mainWindowRef(mainWindow) {

    setWindowTitle(tr("Canvas Control Panel"));
    resize(400, 200);

    tabWidget = new QTabWidget(this);

    // === Tabs ===
    createBackgroundTab();
    tabWidget->addTab(backgroundTab, tr("Background"));
    if (mainWindowRef) {
        createPerformanceTab();
        tabWidget->addTab(performanceTab, tr("Performance"));
        createToolbarTab();
    }
    createButtonMappingTab();
    createControllerMappingTab();
    createKeyboardMappingTab();
    createThemeTab();
    // === Buttons ===
    applyButton = new QPushButton(tr("Apply"));
    okButton = new QPushButton(tr("OK"));
    cancelButton = new QPushButton(tr("Cancel"));

    connect(applyButton, &QPushButton::clicked, this, &ControlPanelDialog::applyChanges);
    connect(okButton, &QPushButton::clicked, this, [=]() {
        applyChanges();
        accept();
    });
    connect(cancelButton, &QPushButton::clicked, this, &ControlPanelDialog::reject);

    // === Layout ===
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tabWidget);
    mainLayout->addLayout(buttonLayout);

    loadFromCanvas();
}


void ControlPanelDialog::createBackgroundTab() {
    backgroundTab = new QWidget(this);

    QLabel *styleLabel = new QLabel(tr("Background Style:"));
    styleCombo = new QComboBox();
    styleCombo->addItem(tr("None"), static_cast<int>(BackgroundStyle::None));
    styleCombo->addItem(tr("Grid"), static_cast<int>(BackgroundStyle::Grid));
    styleCombo->addItem(tr("Lines"), static_cast<int>(BackgroundStyle::Lines));

    QLabel *colorLabel = new QLabel(tr("Background Color:"));
    colorButton = new QPushButton();
    connect(colorButton, &QPushButton::clicked, this, &ControlPanelDialog::chooseColor);

    QLabel *densityLabel = new QLabel(tr("Density:"));
    densitySpin = new QSpinBox();
    densitySpin->setRange(10, 200);
    densitySpin->setSuffix(" px");
    densitySpin->setSingleStep(5);

    QGridLayout *layout = new QGridLayout(backgroundTab);
    layout->addWidget(styleLabel, 0, 0);
    layout->addWidget(styleCombo, 0, 1);
    layout->addWidget(colorLabel, 1, 0);
    layout->addWidget(colorButton, 1, 1);
    layout->addWidget(densityLabel, 2, 0);
    layout->addWidget(densitySpin, 2, 1);
    // layout->setColumnStretch(1, 1); // Stretch the second column
    layout->setRowStretch(3, 1); // Stretch the last row
}

void ControlPanelDialog::chooseColor() {
    QColor chosen = QColorDialog::getColor(selectedColor, this, tr("Select Background Color"));
    if (chosen.isValid()) {
        selectedColor = chosen;
        colorButton->setStyleSheet(QString("background-color: %1").arg(selectedColor.name()));
    }
}

void ControlPanelDialog::applyChanges() {
    if (!canvas) return;

    BackgroundStyle style = static_cast<BackgroundStyle>(
        styleCombo->currentData().toInt()
    );

    canvas->setBackgroundStyle(style);
    canvas->setBackgroundColor(selectedColor);
    canvas->setBackgroundDensity(densitySpin->value());
    canvas->update();
    canvas->saveBackgroundMetadata();

    // ✅ Save these settings as defaults for new tabs
    if (mainWindowRef) {
        mainWindowRef->saveDefaultBackgroundSettings(style, selectedColor, densitySpin->value());
    }

    // ✅ Apply button mappings back to MainWindow with internal keys
    if (mainWindowRef) {
        for (const QString &buttonKey : holdMappingCombos.keys()) {
            QString displayString = holdMappingCombos[buttonKey]->currentText();
            QString internalKey = ButtonMappingHelper::displayToInternalKey(displayString, true);  // true = isDialMode
            mainWindowRef->setHoldMapping(buttonKey, internalKey);
        }
        for (const QString &buttonKey : pressMappingCombos.keys()) {
            QString displayString = pressMappingCombos[buttonKey]->currentText();
            QString internalKey = ButtonMappingHelper::displayToInternalKey(displayString, false);  // false = isAction
            mainWindowRef->setPressMapping(buttonKey, internalKey);
        }

        // ✅ Save to persistent settings
        mainWindowRef->saveButtonMappings();
        
        // ✅ Apply theme settings
        mainWindowRef->setUseCustomAccentColor(useCustomAccentCheckbox->isChecked());
        if (selectedAccentColor.isValid()) {
            mainWindowRef->setCustomAccentColor(selectedAccentColor);
        }
        
        // ✅ Apply color palette setting
        mainWindowRef->setUseBrighterPalette(useBrighterPaletteCheckbox->isChecked());
    }
}

void ControlPanelDialog::loadFromCanvas() {
    styleCombo->setCurrentIndex(static_cast<int>(canvas->getBackgroundStyle()));
    densitySpin->setValue(canvas->getBackgroundDensity());
    selectedColor = canvas->getBackgroundColor();

    colorButton->setStyleSheet(QString("background-color: %1").arg(selectedColor.name()));

    if (mainWindowRef) {
        for (const QString &buttonKey : holdMappingCombos.keys()) {
            QString internalKey = mainWindowRef->getHoldMapping(buttonKey);
            QString displayString = ButtonMappingHelper::internalKeyToDisplay(internalKey, true);  // true = isDialMode
            int index = holdMappingCombos[buttonKey]->findText(displayString);
            if (index >= 0) holdMappingCombos[buttonKey]->setCurrentIndex(index);
        }

        for (const QString &buttonKey : pressMappingCombos.keys()) {
            QString internalKey = mainWindowRef->getPressMapping(buttonKey);
            QString displayString = ButtonMappingHelper::internalKeyToDisplay(internalKey, false);  // false = isAction
            int index = pressMappingCombos[buttonKey]->findText(displayString);
            if (index >= 0) pressMappingCombos[buttonKey]->setCurrentIndex(index);
        }
        
        // Load theme settings
        useCustomAccentCheckbox->setChecked(mainWindowRef->isUsingCustomAccentColor());
        
        // Get the stored custom accent color
        selectedAccentColor = mainWindowRef->getCustomAccentColor();
        
        accentColorButton->setStyleSheet(QString("background-color: %1").arg(selectedAccentColor.name()));
        accentColorButton->setEnabled(useCustomAccentCheckbox->isChecked());
        
        // Load color palette setting
        useBrighterPaletteCheckbox->setChecked(mainWindowRef->isUsingBrighterPalette());
    }
}


void ControlPanelDialog::createPerformanceTab() {
    performanceTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(performanceTab);

    QCheckBox *previewToggle = new QCheckBox(tr("Enable Low-Resolution PDF Previews"));
    previewToggle->setChecked(mainWindowRef->isLowResPreviewEnabled());

    connect(previewToggle, &QCheckBox::toggled, mainWindowRef, &MainWindow::setLowResPreviewEnabled);

    QLabel *note = new QLabel(tr("Disabling this may improve dial smoothness on low-end devices."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray; font-size: 10px;");

    QLabel *dpiLabel = new QLabel(tr("PDF Rendering DPI:"));
    QComboBox *dpiSelector = new QComboBox();
    dpiSelector->addItems({"96", "192", "288", "384", "480"});
    dpiSelector->setCurrentText(QString::number(mainWindowRef->getPdfDPI()));

    connect(dpiSelector, &QComboBox::currentTextChanged, this, [=](const QString &value) {
        mainWindowRef->setPdfDPI(value.toInt());
    });

    QLabel *notePDF = new QLabel(tr("Adjust how the PDF is rendered. Higher DPI means better quality but slower performance. DO NOT CHANGE THIS OPTION WHEN MULTIPLE TABS ARE OPEN. THIS MAY LEAD TO UNDEFINED BEHAVIOR!"));
    notePDF->setWordWrap(true);
    notePDF->setStyleSheet("color: gray; font-size: 10px;");


    layout->addWidget(previewToggle);
    layout->addWidget(note);
    layout->addWidget(dpiLabel);
    layout->addWidget(dpiSelector);
    layout->addWidget(notePDF);

    layout->addStretch();

    // return performanceTab;
}

void ControlPanelDialog::createToolbarTab(){
    toolbarTab = new QWidget(this);
    QVBoxLayout *toolbarLayout = new QVBoxLayout(toolbarTab);

    // ✅ Checkbox to show/hide benchmark controls
    QCheckBox *benchmarkVisibilityCheckbox = new QCheckBox(tr("Show Benchmark Controls"), toolbarTab);
    benchmarkVisibilityCheckbox->setChecked(mainWindowRef->areBenchmarkControlsVisible());
    toolbarLayout->addWidget(benchmarkVisibilityCheckbox);
    QLabel *benchmarkNote = new QLabel(tr("This will show/hide the benchmark controls on the toolbar. Press the clock button to start/stop the benchmark."));
    benchmarkNote->setWordWrap(true);
    benchmarkNote->setStyleSheet("color: gray; font-size: 10px;");
    toolbarLayout->addWidget(benchmarkNote);

    // ✅ Checkbox to show/hide zoom buttons
    QCheckBox *zoomButtonsVisibilityCheckbox = new QCheckBox(tr("Show Zoom Buttons"), toolbarTab);
    zoomButtonsVisibilityCheckbox->setChecked(mainWindowRef->areZoomButtonsVisible());
    toolbarLayout->addWidget(zoomButtonsVisibilityCheckbox);
    QLabel *zoomButtonsNote = new QLabel(tr("This will show/hide the 0.5x, 1x, and 2x zoom buttons on the toolbar"));
    zoomButtonsNote->setWordWrap(true);
    zoomButtonsNote->setStyleSheet("color: gray; font-size: 10px;");
    toolbarLayout->addWidget(zoomButtonsNote);

    QCheckBox *scrollOnTopCheckBox = new QCheckBox(tr("Scroll on Top after Page Switching"), toolbarTab);
    scrollOnTopCheckBox->setChecked(mainWindowRef->isScrollOnTopEnabled());
    toolbarLayout->addWidget(scrollOnTopCheckBox);
    QLabel *scrollNote = new QLabel(tr("Enabling this will make the page scroll to the top after switching to a new page."));
    scrollNote->setWordWrap(true);
    scrollNote->setStyleSheet("color: gray; font-size: 10px;");
    toolbarLayout->addWidget(scrollNote);
    
    toolbarLayout->addStretch();
    toolbarTab->setLayout(toolbarLayout);
    tabWidget->addTab(toolbarTab, tr("Features"));


    // Connect the checkbox
    connect(benchmarkVisibilityCheckbox, &QCheckBox::toggled, mainWindowRef, &MainWindow::setBenchmarkControlsVisible);
    connect(zoomButtonsVisibilityCheckbox, &QCheckBox::toggled, mainWindowRef, &MainWindow::setZoomButtonsVisible);
    connect(scrollOnTopCheckBox, &QCheckBox::toggled, mainWindowRef, &MainWindow::setScrollOnTopEnabled);
}


void ControlPanelDialog::createButtonMappingTab() {
    QWidget *buttonTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(buttonTab);

    QStringList buttonKeys = ButtonMappingHelper::getInternalButtonKeys();
    QStringList buttonDisplayNames = ButtonMappingHelper::getTranslatedButtons();
    QStringList dialModes = ButtonMappingHelper::getTranslatedDialModes();
    QStringList actions = ButtonMappingHelper::getTranslatedActions();

    for (int i = 0; i < buttonKeys.size(); ++i) {
        const QString &buttonKey = buttonKeys[i];
        const QString &buttonDisplayName = buttonDisplayNames[i];
        
        QHBoxLayout *h = new QHBoxLayout();
        h->addWidget(new QLabel(buttonDisplayName));  // Use translated button name

        QComboBox *holdCombo = new QComboBox();
        holdCombo->addItems(dialModes);  // Add translated dial mode names
        holdMappingCombos[buttonKey] = holdCombo;
        h->addWidget(new QLabel(tr("Hold:")));
        h->addWidget(holdCombo);

        QComboBox *pressCombo = new QComboBox();
        pressCombo->addItems(actions);  // Add translated action names
        pressMappingCombos[buttonKey] = pressCombo;
        h->addWidget(new QLabel(tr("Press:")));
        h->addWidget(pressCombo);

        layout->addLayout(h);
    }

    layout->addStretch();
    buttonTab->setLayout(layout);
    tabWidget->addTab(buttonTab, tr("Button Mapping"));
}

void ControlPanelDialog::createKeyboardMappingTab() {
    keyboardTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(keyboardTab);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(tr("Configure custom keyboard shortcuts for application actions:"), keyboardTab);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);
    
    // Table to show current mappings
    keyboardTable = new QTableWidget(0, 2, keyboardTab);
    keyboardTable->setHorizontalHeaderLabels({tr("Key Sequence"), tr("Action")});
    keyboardTable->horizontalHeader()->setStretchLastSection(true);
    keyboardTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyboardTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(keyboardTable);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    addKeyboardMappingButton = new QPushButton(tr("Add Mapping"), keyboardTab);
    removeKeyboardMappingButton = new QPushButton(tr("Remove Mapping"), keyboardTab);
    
    buttonLayout->addWidget(addKeyboardMappingButton);
    buttonLayout->addWidget(removeKeyboardMappingButton);
    buttonLayout->addStretch();
    
    layout->addLayout(buttonLayout);
    
    // Connections
    connect(addKeyboardMappingButton, &QPushButton::clicked, this, &ControlPanelDialog::addKeyboardMapping);
    connect(removeKeyboardMappingButton, &QPushButton::clicked, this, &ControlPanelDialog::removeKeyboardMapping);
    
    // Load current mappings
    if (mainWindowRef) {
        QMap<QString, QString> mappings = mainWindowRef->getKeyboardMappings();
        keyboardTable->setRowCount(mappings.size());
        int row = 0;
        for (auto it = mappings.begin(); it != mappings.end(); ++it) {
            keyboardTable->setItem(row, 0, new QTableWidgetItem(it.key()));
            QString displayAction = ButtonMappingHelper::internalKeyToDisplay(it.value(), false);
            keyboardTable->setItem(row, 1, new QTableWidgetItem(displayAction));
            row++;
        }
    }
    
    tabWidget->addTab(keyboardTab, tr("Keyboard Shortcuts"));
}

void ControlPanelDialog::createThemeTab() {
    themeTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(themeTab);
    
    // Custom accent color
    useCustomAccentCheckbox = new QCheckBox(tr("Use Custom Accent Color"), themeTab);
    layout->addWidget(useCustomAccentCheckbox);
    
    QLabel *accentColorLabel = new QLabel(tr("Accent Color:"), themeTab);
    accentColorButton = new QPushButton(themeTab);
    accentColorButton->setFixedSize(100, 30);
    connect(accentColorButton, &QPushButton::clicked, this, &ControlPanelDialog::chooseAccentColor);
    
    QHBoxLayout *accentColorLayout = new QHBoxLayout();
    accentColorLayout->addWidget(accentColorLabel);
    accentColorLayout->addWidget(accentColorButton);
    accentColorLayout->addStretch();
    layout->addLayout(accentColorLayout);
    
    QLabel *accentColorNote = new QLabel(tr("When enabled, use a custom accent color instead of the system accent color for the toolbar, dial, and tab selection."));
    accentColorNote->setWordWrap(true);
    accentColorNote->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(accentColorNote);
    
    // Enable/disable accent color button based on checkbox
    connect(useCustomAccentCheckbox, &QCheckBox::toggled, accentColorButton, &QPushButton::setEnabled);
    connect(useCustomAccentCheckbox, &QCheckBox::toggled, accentColorLabel, &QLabel::setEnabled);
    
    // Color palette preference
    useBrighterPaletteCheckbox = new QCheckBox(tr("Use Brighter Color Palette"), themeTab);
    layout->addWidget(useBrighterPaletteCheckbox);
    
    QLabel *paletteNote = new QLabel(tr("When enabled, use brighter colors (good for dark PDF backgrounds). When disabled, use darker colors (good for light PDF backgrounds). This setting is independent of the UI theme."));
    paletteNote->setWordWrap(true);
    paletteNote->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(paletteNote);
    
    layout->addStretch();
    
    tabWidget->addTab(themeTab, tr("Theme"));
}

void ControlPanelDialog::chooseAccentColor() {
    QColor chosen = QColorDialog::getColor(selectedAccentColor, this, tr("Select Accent Color"));
    if (chosen.isValid()) {
        selectedAccentColor = chosen;
        accentColorButton->setStyleSheet(QString("background-color: %1").arg(selectedAccentColor.name()));
    }
}

void ControlPanelDialog::addKeyboardMapping() {
    // Step 1: Capture key sequence
    KeyCaptureDialog captureDialog(this);
    if (captureDialog.exec() != QDialog::Accepted) {
        return;
    }
    
    QString keySequence = captureDialog.getCapturedKeySequence();
    if (keySequence.isEmpty()) {
        return;
    }
    
    // Check if key sequence already exists
    if (mainWindowRef && mainWindowRef->getKeyboardMappings().contains(keySequence)) {
        QMessageBox::warning(this, tr("Key Already Mapped"), 
            tr("The key sequence '%1' is already mapped. Please choose a different key combination.").arg(keySequence));
        return;
    }
    
    // Step 2: Choose action
    QStringList actions = ButtonMappingHelper::getTranslatedActions();
    bool ok;
    QString selectedAction = QInputDialog::getItem(this, tr("Select Action"), 
        tr("Choose the action to perform when '%1' is pressed:").arg(keySequence), 
        actions, 0, false, &ok);
    
    if (!ok || selectedAction.isEmpty()) {
        return;
    }
    
    // Convert display name to internal key
    QString internalKey = ButtonMappingHelper::displayToInternalKey(selectedAction, false);
    
    // Add the mapping
    if (mainWindowRef) {
        mainWindowRef->addKeyboardMapping(keySequence, internalKey);
        
        // Update table
        int row = keyboardTable->rowCount();
        keyboardTable->insertRow(row);
        keyboardTable->setItem(row, 0, new QTableWidgetItem(keySequence));
        keyboardTable->setItem(row, 1, new QTableWidgetItem(selectedAction));
    }
}

void ControlPanelDialog::removeKeyboardMapping() {
    int currentRow = keyboardTable->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, tr("No Selection"), tr("Please select a mapping to remove."));
        return;
    }
    
    QTableWidgetItem *keyItem = keyboardTable->item(currentRow, 0);
    if (!keyItem) return;
    
    QString keySequence = keyItem->text();
    
    // Confirm removal
    int ret = QMessageBox::question(this, tr("Remove Mapping"), 
        tr("Are you sure you want to remove the keyboard shortcut '%1'?").arg(keySequence),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        // Remove from MainWindow
        if (mainWindowRef) {
            mainWindowRef->removeKeyboardMapping(keySequence);
        }
        
        // Remove from table
        keyboardTable->removeRow(currentRow);
    }
}

void ControlPanelDialog::createControllerMappingTab() {
    controllerMappingTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(controllerMappingTab);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(tr("Configure physical controller button mappings for your Joy-Con or other controller:"), controllerMappingTab);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);
    
    QLabel *noteLabel = new QLabel(tr("Note: This maps your physical controller buttons to the logical Joy-Con functions used by the application. "
                                     "After setting up the physical mapping, you can configure what actions each logical button performs in the 'Button Mapping' tab."), controllerMappingTab);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: gray; font-size: 10px; margin-bottom: 10px;");
    layout->addWidget(noteLabel);
    
    // Button to open controller mapping dialog
    QPushButton *openMappingButton = new QPushButton(tr("Configure Controller Mapping"), controllerMappingTab);
    openMappingButton->setMinimumHeight(40);
    connect(openMappingButton, &QPushButton::clicked, this, &ControlPanelDialog::openControllerMapping);
    layout->addWidget(openMappingButton);
    
    // Button to reconnect controller
    reconnectButton = new QPushButton(tr("Reconnect Controller"), controllerMappingTab);
    reconnectButton->setMinimumHeight(40);
    reconnectButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }");
    connect(reconnectButton, &QPushButton::clicked, this, &ControlPanelDialog::reconnectController);
    layout->addWidget(reconnectButton);
    
    // Status information
    QLabel *statusLabel = new QLabel(tr("Current controller status:"), controllerMappingTab);
    statusLabel->setStyleSheet("font-weight: bold; margin-top: 20px;");
    layout->addWidget(statusLabel);
    
    // Dynamic status label
    controllerStatusLabel = new QLabel(controllerMappingTab);
    updateControllerStatus();
    layout->addWidget(controllerStatusLabel);
    
    layout->addStretch();
    
    tabWidget->addTab(controllerMappingTab, tr("Controller Mapping"));
}

void ControlPanelDialog::openControllerMapping() {
    if (!mainWindowRef) {
        QMessageBox::warning(this, tr("Error"), tr("MainWindow reference not available."));
        return;
    }
    
    SDLControllerManager *controllerManager = mainWindowRef->getControllerManager();
    if (!controllerManager) {
        QMessageBox::warning(this, tr("Controller Not Available"), 
            tr("Controller manager is not available. Please ensure a controller is connected and restart the application."));
        return;
    }
    
    if (!controllerManager->getJoystick()) {
        QMessageBox::warning(this, tr("No Controller Detected"), 
            tr("No controller is currently connected. Please connect your controller and restart the application."));
        return;
    }
    
    ControllerMappingDialog dialog(controllerManager, this);
    dialog.exec();
}

void ControlPanelDialog::reconnectController() {
    if (!mainWindowRef) {
        QMessageBox::warning(this, tr("Error"), tr("MainWindow reference not available."));
        return;
    }
    
    SDLControllerManager *controllerManager = mainWindowRef->getControllerManager();
    if (!controllerManager) {
        QMessageBox::warning(this, tr("Controller Not Available"), 
            tr("Controller manager is not available."));
        return;
    }
    
    // Show reconnecting message
    controllerStatusLabel->setText(tr("🔄 Reconnecting..."));
    controllerStatusLabel->setStyleSheet("color: orange;");
    
    // Force the UI to update immediately
    QApplication::processEvents();
    
    // Attempt to reconnect using thread-safe method
    QMetaObject::invokeMethod(controllerManager, "reconnect", Qt::BlockingQueuedConnection);
    
    // Update status after reconnection attempt
    updateControllerStatus();
    
    // Show result message
    if (controllerManager->getJoystick()) {
        // Reconnect the controller signals in MainWindow
        mainWindowRef->reconnectControllerSignals();
        
        QMessageBox::information(this, tr("Reconnection Successful"), 
            tr("Controller has been successfully reconnected!"));
    } else {
        QMessageBox::warning(this, tr("Reconnection Failed"), 
            tr("Failed to reconnect controller. Please ensure your controller is powered on and in pairing mode, then try again."));
    }
}

void ControlPanelDialog::updateControllerStatus() {
    if (!mainWindowRef || !controllerStatusLabel) return;
    
    SDLControllerManager *controllerManager = mainWindowRef->getControllerManager();
    if (!controllerManager) {
        controllerStatusLabel->setText(tr("✗ Controller manager not available"));
        controllerStatusLabel->setStyleSheet("color: red;");
        return;
    }
    
    if (controllerManager->getJoystick()) {
        controllerStatusLabel->setText(tr("✓ Controller connected"));
        controllerStatusLabel->setStyleSheet("color: green; font-weight: bold;");
    } else {
        controllerStatusLabel->setText(tr("✗ No controller detected"));
        controllerStatusLabel->setStyleSheet("color: red; font-weight: bold;");
    }
}
