#pragma once

#include "sxpe/commands/bus.hpp"

#include <QAbstractTableModel>
#include <QFont>
#include <QString>
#include <QVector>

#include <vector>

namespace sxpe::gui {

struct DisplayRow {
    std::uint32_t index{0};
    std::uint32_t type{0};
    std::uint32_t group{0};
    std::uint64_t instance{0};
    std::uint32_t ordinal{0};
    std::uint32_t mem_size{0};
    QString tag;
    QString name;
    QString type_h;
    QString group_h;
    QString inst_h;
    bool compressed{false};
    bool deleted{false};
};

class ResourceModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { Tag, Name, Type, Group, Instance, Ordinal, Size, Compressed, Count_ };

    explicit ResourceModel(QObject* parent = nullptr);

    void set_rows(std::vector<sxpe::commands::UiRow> rows);
    void set_visible(QVector<int> visible);
    void sort(int column, Qt::SortOrder order) override;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    [[nodiscard]] const DisplayRow* row_at(int view_row) const;
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
    QFont mono_;
};

QVector<int> filter_rows(const std::vector<DisplayRow>& all, const QString& text,
                         const QString& tag);

}  // namespace sxpe::gui
