#include "crow.h"
#include <filesystem>
#include <thread>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <chrono> 
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression> // Wichtig für Regex

#include "DbManager.h"
#include "MetadataExtractor.h"

namespace fs = std::filesystem;

// Globals
std::atomic<bool> isRunning{true};
std::atomic<int> processedCount{0};

// Paths
const fs::path INBOX_DIR = "uploads";
const fs::path PHOTOS_ROOT = "Photos";

// Helper: String bereinigen
std::string sanitize(const std::string& input) {
    if (input.empty()) return "Unknown";
    std::string out = input;
    std::replace(out.begin(), out.end(), ' ', '_');
    return out;
}

// Helper: C++ Filesystem Time -> QDateTime
QDateTime getFileLastModified(const fs::path& p) {
    try {
        auto ftime = fs::last_write_time(p);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
        return QDateTime::fromSecsSinceEpoch(tt);
    } catch (...) {
        return QDateTime::currentDateTime(); 
    }
}

// Helper: Datum aus Dateinamen extrahieren
// Muster: YYYY-MM-DD_HHMMSS
QDateTime extractDateFromFilename(const std::string& filename) {
    static QRegularExpression re(R"((\d{4})-(\d{2})-(\d{2})_(\d{2})(\d{2})(\d{2}))");
    auto match = re.match(QString::fromStdString(filename));
    
    if (match.hasMatch()) {
        int year  = match.captured(1).toInt();
        int month = match.captured(2).toInt();
        int day   = match.captured(3).toInt();
        int hour  = match.captured(4).toInt();
        int min   = match.captured(5).toInt();
        int sec   = match.captured(6).toInt();
        
        QDate date(year, month, day);
        QTime time(hour, min, sec);
        if (date.isValid() && time.isValid()) {
            return QDateTime(date, time);
        }
    }
    return QDateTime();
}

// Helper: Filename Parsing
struct FileInfo {
    std::string user = "system";
    std::string cleanName;
};

FileInfo parseFilename(const std::string& rawName) {
    FileInfo info;
    info.cleanName = rawName; 
    
    size_t firstSep = rawName.find("___");
    if (firstSep != std::string::npos) {
        info.user = rawName.substr(0, firstSep);
        size_t secondSep = rawName.find("___", firstSep + 3);
        if (secondSep != std::string::npos) {
            info.cleanName = rawName.substr(secondSep + 3);
        } else {
            info.cleanName = rawName.substr(firstSep + 3);
        }
    }
    return info;
}

void workerLoop() {
    // Zielordner Basis erstellen
    if (!fs::exists(PHOTOS_ROOT)) fs::create_directories(PHOTOS_ROOT);
    
    qDebug() << "Worker Loop started. Watching:" << QString::fromStdString(INBOX_DIR.string());

    while(isRunning) {
        if (fs::exists(INBOX_DIR)) {
            auto opts = fs::directory_options::skip_permission_denied;
            
            for (const auto& entry : fs::recursive_directory_iterator(INBOX_DIR, opts)) {
                if (!entry.is_regular_file()) continue;

                try {
                    fs::path srcPath = entry.path();
                    std::string rawName = srcPath.filename().string();
                    if (rawName.starts_with(".")) continue;

                    // 1. Name Parsen
                    FileInfo fInfo = parseFilename(rawName);
                    
                    // 2. Metadaten lesen
                    PhotoData meta = MetadataExtractor::extract(srcPath.string());

                    // 3. Datum ermitteln (Kaskade)
                    QDateTime finalDateTime;
                    if (meta.takenAt.isValid()) {
                        finalDateTime = meta.takenAt;
                    } else {
                        QDateTime nameDate = extractDateFromFilename(fInfo.cleanName);
                        if (nameDate.isValid()) finalDateTime = nameDate;
                        else finalDateTime = getFileLastModified(srcPath);
                    }

                    // --- ÄNDERUNG: PFAD BERECHNUNG ---
                    // Wir spiegeln die Ordnerstruktur aus "uploads"
                    
                    // Pfad relativ zu "uploads/" berechnen
                    // Bsp: srcPath = "uploads/2023/Sommer/img.jpg" -> relObj = "2023/Sommer/img.jpg"
                    fs::path relObj = fs::relative(srcPath, INBOX_DIR);
                    
                    // Wir wollen nur den Ordner-Teil: "2023/Sommer"
                    fs::path relPathStructure = relObj.parent_path();
                    
                    // Zielordner zusammenbauen: "Photos/2023/Sommer"
                    fs::path targetDir = PHOTOS_ROOT / relPathStructure;
                    
                    if (!fs::exists(targetDir)) fs::create_directories(targetDir);
                    
                    fs::path destPath = targetDir / fInfo.cleanName;
                    // ---------------------------------

                    if (fs::exists(destPath)) {
                        fs::remove(destPath);
                    }

                    // 5. Verschieben
                    fs::rename(srcPath, destPath);
                    
                    // Optional: Leere Quell-Ordner löschen? 
                    // Das ist tricky bei recursive_iterator. Wir lassen sie erstmal stehen.

                    // 6. DB Insert
                    WorkerPayload payload;
                    payload.filename = fInfo.cleanName;
                    
                    // In der DB speichern wir den relativen Pfad (z.B. "2023/Sommer")
                    payload.relPath  = relPathStructure.string(); 
                    
                    payload.fullPath = destPath.string();
                    payload.user     = fInfo.user;
                    payload.fileSize = fs::file_size(destPath);
                    payload.fileDate = finalDateTime;
                    payload.meta     = meta;

                    if (DbManager::insertPhoto(payload)) {
                        processedCount++;
                        qDebug() << "Processed:" << QString::fromStdString(fInfo.cleanName) 
                                 << "into" << QString::fromStdString(payload.relPath);
                    } else {
                        qWarning() << "DB Insert failed for" << QString::fromStdString(fInfo.cleanName);
                    }

                } catch (const std::exception& e) {
                    qCritical() << "Error processing file:" << e.what();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::thread t(workerLoop);

    crow::SimpleApp monitor;
    
    CROW_ROUTE(monitor, "/status")
    ([](){
        crow::json::wvalue x;
        x["service"] = "CrowWorker";
        x["processed"] = (int)processedCount;
        x["status"] = "running";
        return x;
    });

    CROW_ROUTE(monitor, "/stop")
    ([&monitor](){
        isRunning = false;
        monitor.stop();
        return "Stopping worker...";
    });

    monitor.port(8081).run();

    if (t.joinable()) t.join();
    return 0;
}