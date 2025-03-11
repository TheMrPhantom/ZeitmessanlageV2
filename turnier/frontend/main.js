const { app, BrowserWindow, globalShortcut, Menu, session } = require('electron');
const path = require('path');

let mainWindow;

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1280,
        height: 720,
        icon: path.join(__dirname, 'public/apple-touch-icon.png'),
        webPreferences: {
            nodeIntegration: true,
        }
    });
    console.log(__dirname);

    Menu.setApplicationMenu(null);

    mainWindow.loadURL('http://127.0.0.1:3000'); // or your app's entry point

    mainWindow.webContents.on('before-input-event', (event, input) => {
        if (input.type === 'keyDown' && input.key === 'F12') {
            mainWindow.webContents.openDevTools();
            event.preventDefault();
        }
    });
}

app.whenReady().then(() => {
    createWindow();

    // Quit application when all windows are closed
    app.on('window-all-closed', () => {
        session.defaultSession.clearStorageData({
            storages: ['cookies'],
        }).then(() => {
            console.log("Cookies cleared on exit.");
            app.quit(); // Quit the app after clearing cookies
        });
    });
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        createWindow();
    }
});
