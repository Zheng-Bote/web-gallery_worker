#include "DbManager.h"
#include <QDebug>
#include <QThread>
#include <QProcessEnvironment> // Nötig für Umgebungsvariablen

// Helper: Keyword ID holen (Code bleibt unverändert)
int DbManager::getOrCreateKeywordId(QSqlDatabase& db, const QString& tag) {
    QSqlQuery q(db);
    // 1. Suche
    q.prepare("SELECT id FROM keywords WHERE tag = :t");
    q.bindValue(":t", tag);
    if (q.exec() && q.next()) return q.value(0).toInt();

    // 2. Insert (Thread-Safe via Conflict)
    q.prepare("INSERT INTO keywords (tag) VALUES (:t) ON CONFLICT (tag) DO NOTHING RETURNING id");
    q.bindValue(":t", tag);
    if (q.exec() && q.next()) return q.value(0).toInt();

    // 3. Fallback
    q.prepare("SELECT id FROM keywords WHERE tag = :t");
    q.bindValue(":t", tag);
    if (q.exec() && q.next()) return q.value(0).toInt();

    return -1;
}

bool DbManager::insertPhoto(const WorkerPayload& p) {
    // Unique Connection Name pro Thread
    QString connName = QString("pg_worker_%1").arg((quint64)QThread::currentThreadId());
    
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL", connName);

        // --- KONFIGURATION AUS UMGEBUNGSVARIABLEN ---
        // Syntax: qEnvironmentVariable("NAME", "DEFAULT_WERT")
        
        QString host = qEnvironmentVariable("PG_HOST", "localhost");
        QString dbName = qEnvironmentVariable("PG_DB", "Photos"); // Default wie gewünscht
        QString user = qEnvironmentVariable("PG_USER", "postgres");
        QString pass = qEnvironmentVariable("PG_PASS");
        QString portStr = qEnvironmentVariable("PG_PORT", "5432");

        // Warnung, falls Passwort fehlt (optional)
        if (pass.isEmpty()) {
            qWarning() << "Warnung: Umgebungsvariable PG_PASS ist nicht gesetzt!";
        }

        db.setHostName(host);
        db.setDatabaseName(dbName);
        db.setUserName(user);
        db.setPassword(pass);
        db.setPort(portStr.toInt());
        // ---------------------------------------------

        if (!db.open()) {
            qCritical() << "DB Open Error (" << host << "/" << dbName << "):" << db.lastError().text();
            return false;
        }

        // Start Transaction
        db.transaction();
        
        bool ok = true;
        qint64 picId = -1;

        // 1. Picture Insert
        QSqlQuery q(db);
        q.prepare("INSERT INTO pictures (file_name, file_path, full_path, file_size, width, height, file_datetime, upload_user) "
                  "VALUES (:fn, :fp, :full, :sz, :w, :h, :dt, :usr) RETURNING id");
        q.bindValue(":fn", QString::fromStdString(p.filename));
        q.bindValue(":fp", QString::fromStdString(p.relPath));
        q.bindValue(":full", QString::fromStdString(p.fullPath));
        q.bindValue(":sz", p.fileSize);
        q.bindValue(":w", p.meta.width);
        q.bindValue(":h", p.meta.height);
        q.bindValue(":dt", p.fileDate);
        q.bindValue(":usr", QString::fromStdString(p.user));
        
        if (q.exec() && q.next()) {
            picId = q.value(0).toLongLong();
        } else {
            ok = false;
            qCritical() << "Insert Picture failed:" << q.lastError().text();
        }

        if (ok) {
            // 2. Location
            QSqlQuery qLoc(db);
            qLoc.prepare("INSERT INTO meta_location (ref_picture, country, country_code, province, city) VALUES (:id, :c, :cc, :p, :ci)");
            qLoc.bindValue(":id", picId);
            qLoc.bindValue(":c", p.meta.country);
            qLoc.bindValue(":cc", p.meta.countryCode);
            qLoc.bindValue(":p", p.meta.province);
            qLoc.bindValue(":ci", p.meta.city);
            if(!qLoc.exec()) ok = false;

            // 3. Exif
            QSqlQuery qExif(db);
            qExif.prepare("INSERT INTO meta_exif (ref_picture, make, model, iso, aperture, exposure_time, gps_latitude, gps_longitude, datetime_original) "
                          "VALUES (:id, :mk, :md, :iso, :ap, :exp, :lat, :lon, :dto)");
            qExif.bindValue(":id", picId);
            qExif.bindValue(":mk", p.meta.make);
            qExif.bindValue(":md", p.meta.model);
            qExif.bindValue(":iso", p.meta.iso);
            qExif.bindValue(":ap", p.meta.aperture);
            qExif.bindValue(":exp", p.meta.exposure);
            qExif.bindValue(":lat", p.meta.gpsLat);
            qExif.bindValue(":lon", p.meta.gpsLon);
            qExif.bindValue(":dto", p.meta.takenAt);
            if(!qExif.exec()) ok = false;

            // 4. IPTC
            QSqlQuery qIptc(db);
            qIptc.prepare("INSERT INTO meta_iptc (ref_picture, object_name, caption, copyright) VALUES (:id, :obj, :cap, :copy)");
            qIptc.bindValue(":id", picId);
            qIptc.bindValue(":obj", p.meta.title);
            qIptc.bindValue(":cap", p.meta.caption);
            qIptc.bindValue(":copy", p.meta.copyright);
            if(!qIptc.exec()) ok = false;

            // 5. Keywords (Many-to-Many)
            for(const auto& k : p.meta.keywords) {
                if(k.trimmed().isEmpty()) continue;
                int kId = getOrCreateKeywordId(db, k.trimmed());
                if(kId > 0) {
                    QSqlQuery qLink(db);
                    qLink.prepare("INSERT INTO picture_keywords (picture_id, keyword_id) VALUES (:pid, :kid) ON CONFLICT DO NOTHING");
                    qLink.bindValue(":pid", picId);
                    qLink.bindValue(":kid", kId);
                    qLink.exec();
                }
            }
        }

        if (ok) {
            db.commit();
            qDebug() << "Successfully processed photo ID:" << picId;
        } else {
            db.rollback();
            qCritical() << "Transaction failed. Rolled back.";
        }
        db.close();
        return ok;
    }
    
    // Aufräumen der Verbindung
    QSqlDatabase::removeDatabase(connName);
    return false;
}