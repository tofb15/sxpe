#pragma once

#include "sxpe/commands/bus.hpp"

#include <QAbstractTableModel>
#include <QString>
#include <QVector>

#include <vector>

namespace sxpe::gui {

class ResourceModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { Tag, Name, Type, Group, Instance, Ordinal, Size, Compressed, Count_ };

    explicit ResourceModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void set_rows(std::vector<sxpe::commands::UiRow> rows);
    void set_visible(QVector<int> visible);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    [[nodiscard]] const sxpe::commands::UiRow* row_at(int view_row) const;
    [[nodiscard]] const std::vector<sxpe::commands::UiRow>& all() const { return all_; }
    [[nodiscard]] int visible_count() const { return visible_.size(); }

private:
    std::vector<sxpe::commands::UiRow> all_;
    QVector<int> visible_;
};

QVector<int> filter_rows(const std::vector<sxpe::commands::UiRow>& all, const QString& text,
                         const QString& tag);

}  // namespace sxpe::gui
