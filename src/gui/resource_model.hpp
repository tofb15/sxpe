#pragma once

#include "sxpe/commands/bus.hpp"

#include <QAbstractItemDelegate>
#include <QAbstractTableModel>
#include <QFont>
#include <QString>
#include <QVector>

#include <cstdint>
#include <vector>

namespace sxpe::gui {

struct DisplayRow {
    std::uint32_t index{0};
    std::uint32_t type{0};
    std::uint32_t group{0};
    std::uint64_t instance{0};
    std::uint32_t ordinal{0};
    std::uint32_t chunk_offset{0};
    std::uint32_t file_size{0};
    std::uint32_t mem_size{0};
    QString id_s;
    QString tag;
    QString name;
    QString type_h;
    QString group_h;
    QString inst_h;
    QString ord_s;
    QString size_s;
    QString cmp_s;
    QString offset_h;
    QString disk_s;
    QString del_s;
    bool compressed{false};
    bool deleted{false};
};

class ResourcePaintDelegate final : public QAbstractItemDelegate {
public:
    explicit ResourcePaintDelegate(QObject* parent = nullptr) : QAbstractItemDelegate(parent) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

class ResourceModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col {
        Id,
        Tag,
        Name,
        Type,
        Group,
        Instance,
        Ordinal,
        Size,
        Compressed,
        Offset,
        Disk,
        Deleted,
        Count_
    };

    explicit ResourceModel(QObject* parent = nullptr);

    void set_rows(std::vector<sxpe::commands::UiRow> rows);
    void set_visible(QVector<int> visible);
    void sort(int column, Qt::SortOrder order) override;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    [[nodiscard]] const DisplayRow* row_at(int view_row) const;
    [[nodiscard]] static const QString& cell_text(const DisplayRow& r, int column);
    [[nodiscard]] int hint_width(int column, const QFontMetrics& fm) const;
    [[nodiscard]] const std::vector<DisplayRow>& all() const { return all_; }
    [[nodiscard]] int visible_count() const { return visible_.size(); }
    [[nodiscard]] int sort_column() const { return sort_col_; }
    [[nodiscard]] Qt::SortOrder sort_order() const { return sort_order_; }

    void apply_sort(QVector<int>& vis) const;

private:
    std::vector<DisplayRow> all_;
    QVector<int> visible_;
    int sort_col_{-1};
    Qt::SortOrder sort_order_{Qt::AscendingOrder};
};

QVector<int> filter_rows(const std::vector<DisplayRow>& all, const QString& text,
                         const QString& tag);

struct ColumnInfo {
    ResourceModel::Col id;
    const char* key;
    const char* header;
    const char* title;
    bool default_on;
};

inline constexpr ColumnInfo kColumnInfo[] = {
    {ResourceModel::Id, "id", "ID", QT_TR_NOOP("ID (load order)"), true},
    {ResourceModel::Tag, "tag", "Tag", QT_TR_NOOP("Tag"), true},
    {ResourceModel::Name, "name", "Name", QT_TR_NOOP("Name"), true},
    {ResourceModel::Type, "type", "Type", QT_TR_NOOP("Type"), true},
    {ResourceModel::Group, "group", "Group", QT_TR_NOOP("Group"), true},
    {ResourceModel::Instance, "instance", "Instance", QT_TR_NOOP("Instance"), true},
    {ResourceModel::Ordinal, "ordinal", "#", QT_TR_NOOP("Ordinal"), true},
    {ResourceModel::Size, "size", "Size", QT_TR_NOOP("Size (uncompressed)"), true},
    {ResourceModel::Compressed, "compressed", "Cmp", QT_TR_NOOP("Compressed"), true},
    {ResourceModel::Offset, "offset", "Offset", QT_TR_NOOP("Chunk offset"), false},
    {ResourceModel::Disk, "disk", "Disk", QT_TR_NOOP("Disk size"), false},
    {ResourceModel::Deleted, "deleted", "Del", QT_TR_NOOP("Deleted"), false},
};

static_assert(sizeof(kColumnInfo) / sizeof(kColumnInfo[0]) == ResourceModel::Count_);

using ColumnMask = std::uint32_t;

[[nodiscard]] ColumnMask default_column_mask();
[[nodiscard]] ColumnMask load_column_mask();
void save_column_mask(ColumnMask m);
[[nodiscard]] bool try_set_column_visible(ColumnMask& m, int col, bool on);
[[nodiscard]] int visible_column_count(ColumnMask m);
[[nodiscard]] const ColumnInfo* column_info(int col);

}  // namespace sxpe::gui
