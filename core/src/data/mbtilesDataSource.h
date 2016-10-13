#pragma once

#include "data/dataSource.h"

namespace SQLite {
class Database;
}

namespace Tangram {

struct MBTilesQueries;
class AsyncWorker;

class MBTilesDataSource : public RawDataSource {
public:

    MBTilesDataSource(std::string _name, std::string _path, std::string _mime);
    ~MBTilesDataSource();

    bool loadTileData(std::shared_ptr<TileTask> _task, TileTaskCb _cb) override;

    void clear() override {}

private:
    bool getTileData(const TileID& _tileId, std::vector<char>& _data);
    void storeTileData(const TileID& _tileId, const std::vector<char>& _data);
    void removePending(const TileID& _tileId);

    void setupMBTiles();
    void initMBTilesSchema(SQLite::Database& db, std::string _name, std::string _mimeType);

    std::string m_name;

    // The path to an mbtiles tile store.
    std::string m_path;
    std::string m_mime;

    // Pointer to SQLite DB of MBTiles store
    std::unique_ptr<SQLite::Database> m_db;
    std::unique_ptr<MBTilesQueries> m_queries;
    std::unique_ptr<AsyncWorker> m_worker;

    std::mutex m_queueMutex;

    std::vector<TileID> m_pending;

};

}
