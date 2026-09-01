#include "resource_model.hpp"

#include <QColor>
#include <QFont>

namespace sxpe::gui {
namespace {

QString hex32(std::uint32_t v) { return QString("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }
QString hex64(std::uint64_t v) { return QString("%1").arg(v, 16, 16, QLatin1Char('0')).toUpper(); }

}  // namespace

void ResourceModel::set_rows(std::vector<sxpe::commands::UiRow> rows) {
    beginResetModel();
    all_ = std::move(rows);
    visible_.resize(static_cast<int>(all_.size()));
    for (int i = 0; i < visible_.size(); ++i) {
        visible_[i] = i;
    }
    endResetModel();
}

void ResourceModel::set_visible(QVector<int> visible) {
    beginResetModel();
    visible_ = std::move(visible);
    endResetModel();
}

int ResourceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : visible_.size();
}
int ResourceModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : Count_;
}

const sxpe::commands::UiRow* ResourceModel::row_at(int view_row) const {
    if (view_row < 0 || view_row >= visible_.size()) {
        return nullptr;
    }
    const int i = visible_[view_row];
    if (i < 0 || static_cast<size_t>(i) >= all_.size()) {
        return nullptr;
    }
    return &all_[static_cast<size_t>(i)];
}

QVariant ResourceModel::data(const QModelIndex& index, int role) const {
    const auto* r = row_at(index.row());
    if (!r) {
        return {};
    }
    if (role == Qt::ForegroundRole && r->deleted) {
        return QColor(128, 128, 128);
    }
    if (role == Qt::FontRole && (index.column() == Type || index.column() == Group ||
                                 index.column() == Instance || index.column() == Ordinal)) {
        QFont f;
        f.setStyleHint(QFont::Monospace);
        f.setFamily(QStringLiteral("Consolas"));
        return f;
    }
    if (role == Qt::ToolTipRole) {
        return QString("%1-%2-%3 #%4")
            .arg(hex32(r->type), hex32(r->group), hex64(r->instance))
            .arg(r->ordinal);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
        case Tag:
            return QString::fromStdString(r->tag);
        case Name:
            return QString::fromStdString(r->name);
        case Type:
            return hex32(r->type);
        case Group:
            return hex32(r->group);
        case Instance:
            return hex64(r->instance);
        case Ordinal:
            return QString::number(r->ordinal);
        case Size:
            return QString::number(r->mem_size);
        case Compressed:
            return r->compressed ? QStringLiteral("Y") : QString();
        default:
            return {};
    }
}

QVariant ResourceModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    static const char* k[] = {"Tag", "Name", "Type", "Group", "Instance", "#", "Size", "Cmp"};
    return section >= 0 && section < Count_ ? QString::fromLatin1(k[section]) : QVariant{};
}

QVector<int> filter_rows(const std::vector<sxpe::commands::UiRow>& all, const QString& text,
                         const QString& tag) {
    QVector<int> out;
    out.reserve(static_cast<int>(all.size()));
    const auto needle = text.trimmed();
    const auto want = tag.trimmed();
    const bool any_tag = want.isEmpty() || want == QLatin1String("All");
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        const auto& r = all[static_cast<size_t>(i)];
        if (!any_tag) {
            const auto t = QString::fromStdString(r.tag);
            if (t.compare(want, Qt::CaseInsensitive) != 0 &&
                !(want == QLatin1String("IMG") && t == QLatin1String("_IMG"))) {
                continue;
            }
        }
        if (!needle.isEmpty()) {
            const auto name = QString::fromStdString(r.name);
            const auto type = QString("%1").arg(r.type, 8, 16, QLatin1Char('0'));
            if (!name.contains(needle, Qt::CaseInsensitive) &&
                !type.contains(needle, Qt::CaseInsensitive) &&
                !QString::fromStdString(r.tag).contains(needle, Qt::CaseInsensitive)) {
                continue;
            }
        }
        out.push_back(i);
    }
    return out;
}

}  // namespace sxpe::gui
