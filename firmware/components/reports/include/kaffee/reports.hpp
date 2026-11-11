#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kaffee/domain.hpp"

// Report generation and streaming CSV export.
//
// The component is pure C++17 and host-testable: it only reads a `Ledger` and
// writes through a caller supplied sink. No report is buffered as a whole file.
//
// Period boundaries use Europe/Berlin wall-clock time and the EU DST rule
// (last Sunday of March 01:00 UTC until last Sunday of October 01:00 UTC).
// `Period::from` is inclusive, `Period::to` is exclusive.
namespace kaffee::reports {

enum class PeriodKind { Week, Month, Year, All, Custom };

enum class ReportKind {
    Overview,
    Consumption,
    Deposits,
    Expenses,
    Cashflow,
    Balances,
    Transactions,
};

enum class CsvFormat {
    Excel,  // UTF-8 BOM, sep=;, semicolon, CRLF, German decimals/dates
    Raw,    // comma, dot, ISO 8601
};

struct Period {
    Timestamp from = 0;
    Timestamp to = 0;  // exclusive
    PeriodKind kind = PeriodKind::Custom;
};

// ISO week Monday 00:00 Europe/Berlin.
Period week(Timestamp now);
// Calendar month in Europe/Berlin.
Period month(Timestamp now);
// Calendar year in Europe/Berlin.
Period year(Timestamp now);
// Everything; `from` is 0 and `to` is the largest timestamp.
Period all();
Period custom(Timestamp from, Timestamp to);
// Dispatch for the four standard kinds. `Custom` returns an empty (invalid)
// period because it needs explicit bounds.
Period period(PeriodKind kind, Timestamp now);

bool contains(const Period& period, Timestamp ts);
bool valid(const Period& period);

// Civil time in Europe/Berlin. `weekday` is ISO 1=Monday ... 7=Sunday.
struct Civil {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int weekday = 4;
};

Civil to_berlin(Timestamp utc);
// Converts a Berlin wall-clock value to UTC. Ambiguous/inexistent values only
// occur around 02:00-03:00, never at a period boundary.
Timestamp from_berlin(const Civil& local);
bool berlin_is_dst(Timestamp utc);
// 3600 (CET) or 7200 (CEST).
int berlin_utc_offset(Timestamp utc);

enum class ColumnType {
    Text,
    MoneyEur,    // cents rendered as EUR with two decimals
    MoneyCents,  // cents rendered as an integer
    Date,        // date only
    DateTime,    // date and time
    Integer,
    Bool,
};

struct Column {
    std::string_view key;    // stable raw header, e.g. "amount_cents"
    std::string_view label;  // German Excel header
    ColumnType type = ColumnType::Text;
};

struct Value {
    std::string text;
    Cents cents = 0;
    Timestamp ts = 0;
    int64_t number = 0;
    bool flag = false;

    static Value of_text(std::string value);
    static Value money(Cents value);
    static Value date(Timestamp value);
    static Value datetime(Timestamp value);
    static Value integer(int64_t value);
    static Value boolean(bool value);
};

using RowSink = std::function<bool(const std::vector<Value>&)>;
using WriteFn = std::function<bool(std::string_view)>;

struct Sink {
    WriteFn write;
};

// Columns for a report. The same table is used by JSON and CSV consumers;
// only the header labels differ (`key` for raw, `label` for Excel).
const std::vector<Column>& columns(ReportKind kind);

// Emits the header-less rows one at a time. The sink returns false to abort.
// No report accumulates the full result set; transaction and expense rows are
// produced directly from the ledger event stream.
Result<Unit> for_each_row(const Ledger& ledger, ReportKind kind, const Period& period,
                          const RowSink& sink);

// Streams a complete CSV document (BOM/delimiter line, header, rows) to the
// sink. The sink is never handed the whole file at once.
Result<Unit> write_csv(const Ledger& ledger, ReportKind kind, const Period& period,
                       CsvFormat format, const Sink& sink);

// Low-level writer, reusable by the API for other tables.
class CsvWriter {
  public:
    CsvWriter(CsvFormat format, Sink sink);

    // UTF-8 BOM and the `sep=;` hint for Excel; nothing for raw.
    bool begin();
    bool header(const std::vector<Column>& columns);
    bool row(const std::vector<Column>& columns, const std::vector<Value>& values);
    bool end();

    bool ok() const {
        return ok_;
    }

  private:
    bool emit(std::string_view chunk);

    CsvFormat format_;
    Sink sink_;
    bool ok_ = true;
    bool began_ = false;
};

std::string format_money(Cents cents, CsvFormat format);
std::string format_date(Timestamp ts, CsvFormat format);
std::string format_datetime(Timestamp ts, CsvFormat format);
// RFC 4180 quoting for the format's delimiter.
std::string escape_field(std::string_view value, CsvFormat format);
// Prefixes cells that would start a spreadsheet formula, unless the text is a
// valid number. Numeric column types are never touched.
std::string protect_formula(std::string_view value);

std::string report_kind_name(ReportKind kind);
std::optional<ReportKind> report_kind_from_name(std::string_view name);
std::string period_kind_name(PeriodKind kind);
std::optional<PeriodKind> period_kind_from_name(std::string_view name);

const char* csv_content_type(CsvFormat format);
// `kaffeekasse_YYYY-MM_<report>.csv`, `kaffeekasse_all_<report>.csv` or
// `kaffeekasse_YYYY-MM-DD_YYYY-MM-DD_<report>.csv` for custom periods.
std::string report_filename(ReportKind kind, const Period& period);

// Monthly counts net of voids; delegates to Ledger::leaderboard so a late void
// is attributed to the original booking's period.
Leaderboard leaderboard(const Ledger& ledger, const Period& period);
Leaderboard monthly_leaderboard(const Ledger& ledger, Timestamp now);

}  // namespace kaffee::reports
