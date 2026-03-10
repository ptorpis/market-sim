#pragma once

#include <string>

enum class PersistenceBackend { CSV, Postgres, Both };

struct WriterConfig {
    PersistenceBackend backend = PersistenceBackend::CSV;
    std::string connection_string;
    int batch_size = 1000;
};
