CREATE TABLE runs (
    run_id      UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    started_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    config      JSONB       NOT NULL
);

CREATE TABLE order_deltas (
    run_id        UUID    NOT NULL REFERENCES runs(run_id),
    timestamp     BIGINT  NOT NULL,
    sequence_num  BIGINT  NOT NULL,
    delta_type    TEXT    NOT NULL,
    order_id      BIGINT  NOT NULL,
    client_id     BIGINT  NOT NULL,
    instrument_id INTEGER NOT NULL,
    side          TEXT    NOT NULL,
    price         BIGINT  NOT NULL,
    quantity      BIGINT  NOT NULL,
    remaining_qty BIGINT  NOT NULL,
    trade_id      BIGINT,
    new_order_id  BIGINT,
    new_price     BIGINT,
    new_quantity  BIGINT
);
CREATE INDEX ON order_deltas (run_id, timestamp);

CREATE TABLE trades (
    run_id          UUID    NOT NULL REFERENCES runs(run_id),
    timestamp       BIGINT  NOT NULL,
    trade_id        BIGINT  NOT NULL,
    instrument_id   INTEGER NOT NULL,
    buyer_id        BIGINT  NOT NULL,
    seller_id       BIGINT  NOT NULL,
    buyer_order_id  BIGINT  NOT NULL,
    seller_order_id BIGINT  NOT NULL,
    price           BIGINT  NOT NULL,
    quantity        BIGINT  NOT NULL,
    aggressor_side  TEXT    NOT NULL,
    fair_price      BIGINT  NOT NULL
);
CREATE INDEX ON trades (run_id, timestamp);

CREATE TABLE pnl_snapshots (
    run_id         UUID   NOT NULL REFERENCES runs(run_id),
    timestamp      BIGINT NOT NULL,
    client_id      BIGINT NOT NULL,
    long_position  BIGINT NOT NULL,
    short_position BIGINT NOT NULL,
    cash           BIGINT NOT NULL,
    fair_price     BIGINT NOT NULL
);
CREATE INDEX ON pnl_snapshots (run_id, timestamp);

CREATE TABLE market_state (
    run_id     UUID   NOT NULL REFERENCES runs(run_id),
    timestamp  BIGINT NOT NULL,
    fair_price BIGINT NOT NULL,
    best_bid   BIGINT NOT NULL,
    best_ask   BIGINT NOT NULL
);
CREATE INDEX ON market_state (run_id, timestamp);
