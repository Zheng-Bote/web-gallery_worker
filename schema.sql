-- Tabellen löschen falls Neustart gewünscht
DROP TABLE IF EXISTS picture_keywords;
DROP TABLE IF EXISTS keywords;
DROP TABLE IF EXISTS meta_xmp;
DROP TABLE IF EXISTS meta_iptc;
DROP TABLE IF EXISTS meta_exif;
DROP TABLE IF EXISTS meta_location;
DROP TABLE IF EXISTS pictures;

-- 1. Basis Bild-Daten
CREATE TABLE pictures (
    id SERIAL PRIMARY KEY,
    file_name TEXT NOT NULL,
    file_path TEXT NOT NULL,       -- Relativer Pfad
    full_path TEXT NOT NULL,       -- Absoluter Pfad
    file_size BIGINT,
    width INTEGER,
    height INTEGER,
    file_datetime TIMESTAMP,
    imported_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    upload_user TEXT,
    access_group TEXT DEFAULT 'public'
);

-- 2. Location
CREATE TABLE meta_location (
    id SERIAL PRIMARY KEY,
    ref_picture INTEGER NOT NULL REFERENCES pictures(id) ON DELETE CASCADE,
    continent TEXT,
    country TEXT,
    country_code TEXT,
    province TEXT,
    city TEXT,
    district TEXT
);

-- 3. Exif
CREATE TABLE meta_exif (
    id SERIAL PRIMARY KEY,
    ref_picture INTEGER NOT NULL REFERENCES pictures(id) ON DELETE CASCADE,
    make TEXT,
    model TEXT,
    aperture TEXT,
    iso TEXT,
    exposure_time TEXT,
    gps_latitude DOUBLE PRECISION,
    gps_longitude DOUBLE PRECISION,
    gps_altitude DOUBLE PRECISION,
    datetime_original TIMESTAMP
);

-- 4. Iptc
CREATE TABLE meta_iptc (
    id SERIAL PRIMARY KEY,
    ref_picture INTEGER NOT NULL REFERENCES pictures(id) ON DELETE CASCADE,
    object_name TEXT,
    caption TEXT,
    copyright TEXT,
    byline TEXT
);

-- 5. Xmp
CREATE TABLE meta_xmp (
    id SERIAL PRIMARY KEY,
    ref_picture INTEGER NOT NULL REFERENCES pictures(id) ON DELETE CASCADE,
    title TEXT,
    description TEXT,
    rating INTEGER,
    label TEXT
);

-- 6. Keywords Pool
CREATE TABLE keywords (
    id SERIAL PRIMARY KEY,
    tag TEXT NOT NULL UNIQUE
);

-- 7. Many-to-Many Link
CREATE TABLE picture_keywords (
    picture_id INTEGER NOT NULL REFERENCES pictures(id) ON DELETE CASCADE,
    keyword_id INTEGER NOT NULL REFERENCES keywords(id) ON DELETE CASCADE,
    PRIMARY KEY (picture_id, keyword_id)
);