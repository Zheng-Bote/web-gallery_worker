#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include "MetadataExtractor.h"

struct WorkerPayload {
    std::string filename; // Bereinigt
    std::string relPath;
    std::string fullPath;
    std::string user;
    long long fileSize;
    QDateTime fileDate;
    PhotoData meta;
};

class DbManager {
public:
    static bool insertPhoto(const WorkerPayload& p);
private:
    static int getOrCreateKeywordId(QSqlDatabase& db, const QString& tag);
};