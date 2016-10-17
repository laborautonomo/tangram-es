#include "data/mbtilesDataSource.h"

#include "util/asyncWorker.h"
#include "log.h"

#include <SQLiteCpp/Database.h>
#include "hash-library/md5.cpp"


namespace Tangram {

/**
 * The schema.sql used to set up an MBTiles Database.
 *
 * https://github.com/mapbox/node-mbtiles/blob/4bbfaf991969ce01c31b95184c4f6d5485f717c3/lib/schema.sql
 */
const char* SCHEMA = R"SQL_ESC(BEGIN;

CREATE TABLE IF NOT EXISTS map (
   zoom_level INTEGER,
   tile_column INTEGER,
   tile_row INTEGER,
   tile_id TEXT,
   grid_id TEXT
);

CREATE TABLE IF NOT EXISTS grid_key (
    grid_id TEXT,
    key_name TEXT
);

CREATE TABLE IF NOT EXISTS keymap (
    key_name TEXT,
    key_json TEXT
);

CREATE TABLE IF NOT EXISTS grid_utfgrid (
    grid_id TEXT,
    grid_utfgrid BLOB
);

CREATE TABLE IF NOT EXISTS images (
    tile_data blob,
    tile_id text
);

CREATE TABLE IF NOT EXISTS metadata (
    name text,
    value text
);

CREATE TABLE IF NOT EXISTS geocoder_data (
    type TEXT,
    shard INTEGER,
    data BLOB
);

CREATE UNIQUE INDEX IF NOT EXISTS map_index ON map (zoom_level, tile_column, tile_row);
CREATE UNIQUE INDEX IF NOT EXISTS grid_key_lookup ON grid_key (grid_id, key_name);
CREATE UNIQUE INDEX IF NOT EXISTS keymap_lookup ON keymap (key_name);
CREATE UNIQUE INDEX IF NOT EXISTS grid_utfgrid_lookup ON grid_utfgrid (grid_id);
CREATE UNIQUE INDEX IF NOT EXISTS images_id ON images (tile_id);
CREATE UNIQUE INDEX IF NOT EXISTS name ON metadata (name);
CREATE INDEX IF NOT EXISTS map_grid_id ON map (grid_id);
CREATE INDEX IF NOT EXISTS geocoder_type_index ON geocoder_data (type);
CREATE UNIQUE INDEX IF NOT EXISTS geocoder_shard_index ON geocoder_data (type, shard);

CREATE VIEW IF NOT EXISTS tiles AS
    SELECT
        map.zoom_level AS zoom_level,
        map.tile_column AS tile_column,
        map.tile_row AS tile_row,
        images.tile_data AS tile_data
    FROM map
    JOIN images ON images.tile_id = map.tile_id;

CREATE VIEW IF NOT EXISTS grids AS
    SELECT
        map.zoom_level AS zoom_level,
        map.tile_column AS tile_column,
        map.tile_row AS tile_row,
        grid_utfgrid.grid_utfgrid AS grid
    FROM map
    JOIN grid_utfgrid ON grid_utfgrid.grid_id = map.grid_id;

CREATE VIEW IF NOT EXISTS grid_data AS
    SELECT
        map.zoom_level AS zoom_level,
        map.tile_column AS tile_column,
        map.tile_row AS tile_row,
        keymap.key_name AS key_name,
        keymap.key_json AS key_json
    FROM map
    JOIN grid_key ON map.grid_id = grid_key.grid_id
    JOIN keymap ON grid_key.key_name = keymap.key_name;
COMMIT;)SQL_ESC";

struct MBTilesQueries {
    // SELECT statement from tiles view
    SQLite::Statement getTileData;

    // REPLACE INTO statement in map table
    SQLite::Statement putMap;

    // REPLACE INTO statement in images table
    SQLite::Statement putImage;

    MBTilesQueries(SQLite::Database& _db)
        : getTileData(_db, "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;"),
          putMap(_db, "REPLACE INTO map (zoom_level, tile_column, tile_row, tile_id) VALUES (?, ?, ?, ?);" ),
          putImage(_db, "REPLACE INTO images (tile_id, tile_data) VALUES (?, ?);" ) {}

};

MBTilesDataSource::MBTilesDataSource(std::string _name, std::string _path,
                                     std::string _mime, bool _offlineCache)
    : m_name(_name), m_path(_path), m_mime(_mime), m_offlineMode(_offlineCache) {

    m_worker = std::make_unique<AsyncWorker>();

    setupMBTiles();
}

MBTilesDataSource::~MBTilesDataSource() {
    // TODO teardown db?
}

bool MBTilesDataSource::loadTileData(std::shared_ptr<TileTask> _task, TileTaskCb _cb) {

    if (m_offlineMode) {
        // Try next source
        _task->rawSource = next->level;

        return loadNextSource(_task, _cb);
    }

    if (!m_db) { return false; }

    if (_task->rawSource == this->level) {

        m_worker->enqueue([this, _task, _cb](){
            TileID tileId = _task->tileId();

            auto& task = static_cast<DownloadTileTask&>(*_task);
            task.rawTileData = std::make_shared<std::vector<char>>();

            getTileData(tileId, *task.rawTileData);

            if (task.hasData()) {
                LOGW("loaded tile: %s, %d", tileId.toString().c_str(), task.rawTileData->size());

                _cb.func(_task);

            } else if (next) {

                // Don't try this source again
                _task->rawSource = next->level;

                if (!loadNextSource(_task, _cb)) {
                    // Trigger TileManager update so that tile will be
                    // downloaded next time.
                    _task->setNeedsLoading(true);
                    requestRender();
                }
            }
        });
        return true;
    }

    return loadNextSource(_task, _cb);
}

bool MBTilesDataSource::loadNextSource(std::shared_ptr<TileTask> _task, TileTaskCb _cb) {
    if (!next) { return false; }

    if (!m_db) {
        return next->loadTileData(_task, _cb);
    }

    // Intercept TileTaskCb to store result from next source.
    TileTaskCb cb{[this, _cb](std::shared_ptr<TileTask> _task) {

        if (_task->hasData()) {

            m_worker->enqueue([this, _task](){

                auto& task = static_cast<DownloadTileTask&>(*_task);

                LOGW("store tile: %s, %d", _task->tileId().toString().c_str(), task.hasData());

                storeTileData(_task->tileId(), *task.rawTileData);
            });
            _cb.func(_task);

        } else if (m_offlineMode) {
            LOGW("try fallback tile: %s, %d", _task->tileId().toString().c_str());

            m_worker->enqueue([this, _task, _cb](){

                auto& task = static_cast<DownloadTileTask&>(*_task);
                task.rawTileData = std::make_shared<std::vector<char>>();

                getTileData(_task->tileId(), *task.rawTileData);

                LOGW("loaded tile: %s, %d", _task->tileId().toString().c_str(), task.rawTileData->size());

                _cb.func(_task);

            });
        } else {
            LOGW("missing tile: %s, %d", _task->tileId().toString().c_str());
            _cb.func(_task);
        }
    }};

    return next->loadTileData(_task, cb);
}

void MBTilesDataSource::setupMBTiles() {
    // If we have a path to an MBTiles file, try to open up a SQLite database
    // instance.
    try {
        // Need to explicitly open a SQLite DB with OPEN_READWRITE and
        // OPEN_CREATE flags to make a file and write.
        m_db = std::make_unique<SQLite::Database>(m_path,
                                                  SQLite::OPEN_READWRITE |
                                                  SQLite::OPEN_CREATE);

        LOG("MBTiles SQLite DB Opened at: %s", m_path.c_str());

        // If needed, setup the database by running the schema.sql.
        initMBTilesSchema(*m_db, m_name, m_mime);

        m_queries = std::make_unique<MBTilesQueries>(*m_db);

    } catch (std::exception& e) {
        LOGE("Unable to open SQLite database: %s", e.what());

        m_db.reset();
        return;
    }

    LOGN("Opened SQLite database: %s", m_path.c_str());
}

/**
 * We check to see if the database has the MBTiles Schema.
 * If not, we execute the schema SQL.
 *
 * @param _source A pointer to a the data source in which we will setup a db.
 */
void MBTilesDataSource::initMBTilesSchema(SQLite::Database& db, std::string _name, std::string _mimeType) {

    bool map = false, grid_key = false, keymap = false, grid_utfgrid = false, images = false,
         metadata = false, geocoder_data = false, tiles = false, grids = false, grid_data = false;

    try {
        SQLite::Statement query(db, "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')");
        while (query.executeStep()) {
            std::string name = query.getColumn(0);
            if (name == "map") map = true;
            else if (name == "grid_key") grid_key = true;
            else if (name == "keymap") keymap = true;
            else if (name == "grid_utfgrid") grid_utfgrid = true;
            else if (name == "images") images = true;
            else if (name == "metadata") metadata = true;
            else if (name == "geocoder_data") geocoder_data = true;
            else if (name == "tiles") tiles = true;
            else if (name == "grids") grids = true;
            else if (name == "grid_data") grid_data = true;
        }
    } catch (std::exception& e) {
        LOGE("Unable to check schema of SQLite MBTiles database: %s", e.what());
    }

    // Return if we have all the tables and views that should exist.
    if (map && grid_key && keymap && grid_utfgrid && images &&
            metadata && geocoder_data && tiles && grids && grid_data) {
        return;
    }

    // Otherwise, we need to execute schema.sql to set up the db with the right schema.
    try {
        // Execute schema.
        db.exec(SCHEMA);

        // Fill in metadata table.
        // https://github.com/pnorman/mbtiles-spec/blob/2.0/2.0/spec.md#content
        // https://github.com/mapbox/mbtiles-spec/pull/46
        SQLite::Statement stmt(db, "REPLACE INTO metadata (name, value) VALUES (?, ?);");

        // name, type, version, description, format, compression
        stmt.bind(1, "name");
        stmt.bind(2, _name);
        stmt.exec();
        stmt.reset();

        stmt.bind(1, "type");
        stmt.bind(2, "baselayer");
        stmt.exec();
        stmt.reset();

        stmt.bind(1, "version");
        stmt.bind(2, 1);
        stmt.exec();
        stmt.reset();

        stmt.bind(1, "description");
        stmt.bind(2, "MBTiles tile container created by Tangram ES.");
        stmt.exec();
        stmt.reset();

        stmt.bind(1, "format");
        stmt.bind(2, _mimeType);
        stmt.exec();
        stmt.reset();

        // Compression not yet implemented.
        // http://www.iana.org/assignments/http-parameters/http-parameters.xhtml#content-coding
        // identity means no compression
        stmt.bind(1, "compression");
        stmt.bind(2, "identity");
        stmt.exec();

    } catch (std::exception& e) {
        LOGE("Unable to setup SQLite MBTiles database: %s", e.what());
    }
}

bool MBTilesDataSource::getTileData(const TileID& _tileId, std::vector<char>& _data) {
    try {
        // Google TMS to WMTS
        // https://github.com/mapbox/node-mbtiles/blob/
        // 4bbfaf991969ce01c31b95184c4f6d5485f717c3/lib/mbtiles.js#L149
        int z = _tileId.z;
        int y = (1 << z) - 1 - _tileId.y;

        auto& stmt = m_queries->getTileData;
        stmt.bind(1, z);
        stmt.bind(2, _tileId.x);
        stmt.bind(3, y);

        if (stmt.executeStep()) {
            SQLite::Column column = stmt.getColumn(0);
            const char* blob = (const char*) column.getBlob();
            const int length = column.getBytes();
            _data.resize(length);
            memcpy(_data.data(), blob, length);

            stmt.reset();
            return true;
        }

        stmt.reset();

    } catch (std::exception& e) {
        LOGE("MBTiles SQLite get tile_data statement failed: %s", e.what());
    }

    return false;
}

void MBTilesDataSource::storeTileData(const TileID& _tileId, const std::vector<char>& _data) {
    int z = _tileId.z;
    int y = (1 << z) - 1 - _tileId.y;

    const char* data = _data.data();
    size_t size = _data.size();

    /**
     * We create an MD5 of the raw tile data. The MD5 functions as a hash
     * between the map and images tables. With this, tiles with duplicate
     * data will join to a single entry in the images table.
     */
    MD5 md5;
    std::string md5id = md5(data, size);

    try {
        auto& stmt = m_queries->putMap;
        stmt.bind(1, z);
        stmt.bind(2, _tileId.x);
        stmt.bind(3, y);
        stmt.bind(4, md5id);
        stmt.exec();

        stmt.reset();

    } catch (std::exception& e) {
        LOGE("MBTiles SQLite put map statement failed: %s", e.what());
    }

    try {
        auto& stmt = m_queries->putImage;
        stmt.bind(1, md5id);
        stmt.bind(2, data, size);
        stmt.exec();

        stmt.reset();

    } catch (std::exception& e) {
        LOGE("MBTiles SQLite put image statement failed: %s", e.what());
    }
}

}
