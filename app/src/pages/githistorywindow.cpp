#include "githistorywindow.h"
#include "ui/theme.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QSyntaxHighlighter>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int PageSize = 200;
const QString RefFormat = QStringLiteral("--format=%(refname)%00%(objectname)%00%(*objectname)%00%(objecttype)%00%(*objecttype)");
QLabel *plainLabel(QWidget *parent) {
    auto *label = new QLabel(parent); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse); return label;
}
void tableStyle(QTableView *table) {
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false); table->setWordWrap(false);
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(30);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->horizontalHeader()->setStretchLastSection(false);
}
class DiffHighlighter final : public QSyntaxHighlighter {
public:
    explicit DiffHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
    void highlightBlock(const QString &text) override {
        QTextCharFormat format;
        if (text.startsWith(QStringLiteral("+++")) || text.startsWith(QStringLiteral("---")) || text.startsWith(QStringLiteral("diff "))) {
            format.setForeground(QColor(UiTheme::Text)); format.setFontWeight(QFont::DemiBold);
        } else if (text.startsWith('+')) { format.setForeground(QColor(UiTheme::Success)); format.setBackground(QColor(UiTheme::SuccessBackground)); }
        else if (text.startsWith('-')) { format.setForeground(QColor(UiTheme::Danger)); format.setBackground(QColor(UiTheme::DangerBackground)); }
        else if (text.startsWith(QStringLiteral("@@"))) { format.setForeground(QColor(UiTheme::Accent)); format.setBackground(QColor(UiTheme::Selection)); }
        else return;
        setFormat(0, text.size(), format);
    }
};
struct Edge { int from; int to; bool upper; int color; };
struct GraphRow { int lane = 0; int color = 0; QList<Edge> edges; };
QColor laneColor(int lane) {
    static const char *colors[]{UiTheme::Accent, UiTheme::Success, UiTheme::Warning, "#805889", "#32747b", UiTheme::Danger};
    return QColor(colors[lane % 6]);
}
}

// Original lane layout; parent identities, not branch names, determine edges.
class GitGraphModel final : public QAbstractTableModel {
public:
    QList<Git::Commit> commits;
    QList<GraphRow> graph;
    QMap<QString, QStringList> labels;
    int lanes = 1;
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : commits.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 6; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        return QStringList{QStringLiteral("提交图"), QStringLiteral("提交说明"), QStringLiteral("分支 / 标签"),
            QStringLiteral("作者"), QStringLiteral("时间"), QStringLiteral("提交")}.value(section);
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= commits.size()) return {};
        const auto &c = commits[index.row()];
        if (role == Qt::ToolTipRole) return c.id + QLatin1Char('\n') + c.subject + QLatin1Char('\n') + labels.value(c.id).join(QStringLiteral("\n"));
        if (role == Qt::AccessibleTextRole && index.column() == 0)
            return QStringLiteral("%1 个父提交：%2").arg(c.parents.size()).arg(c.parents.join(QStringLiteral("，")));
        if (role != Qt::DisplayRole) return {};
        switch (index.column()) {
        case 1: return c.subject;
        case 2: return labels.value(c.id).join(QStringLiteral(" · "));
        case 3: return c.author;
        case 4: return c.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        case 5: return c.id.left(10);
        }
        return {};
    }
    void replace(QList<Git::Commit> values, const QList<Git::Reference> &refs, const QString &head) {
        beginResetModel(); commits = std::move(values); labels.clear(); graph.clear(); lanes = 1;
        for (const auto &ref : refs) {
            QString name = ref.name;
            if (name.startsWith(QStringLiteral("refs/heads/"))) name.remove(0, 11);
            else if (name.startsWith(QStringLiteral("refs/remotes/"))) name.remove(0, 13);
            else if (name.startsWith(QStringLiteral("refs/tags/"))) name = QStringLiteral("标签: ") + name.mid(10);
            labels[ref.id].append(name);
        }
        if (!head.isEmpty()) labels[head].prepend(QStringLiteral("HEAD"));
        QStringList active;
        QMap<QString, int> colors;
        int nextColor = 0;
        for (const auto &c : commits) {
            int lane = active.indexOf(c.id);
            const auto before = active;
            if (lane < 0) { lane = active.size(); active.append(c.id); colors[c.id] = nextColor++; }
            GraphRow row; row.lane = lane; row.color = colors.value(c.id);
            for (int i = 0; i < before.size(); ++i) row.edges.append({i, i, true, colors.value(before[i])});
            const auto entering = active;
            active.removeAt(lane);
            int position = lane;
            for (int p = 0; p < c.parents.size(); ++p) {
                const auto id = c.parents[p];
                if (!active.contains(id)) {
                    active.insert(qMin(position++, int(active.size())), id);
                    colors[id] = p == 0 ? row.color : nextColor++;
                }
            }
            for (int i = 0; i < entering.size(); ++i) {
                if (i == lane) continue;
                row.edges.append({i, int(active.indexOf(entering[i])), false, colors.value(entering[i])});
            }
            for (const auto &p : c.parents) row.edges.append({lane, int(active.indexOf(p)), false, colors.value(p)});
            lanes = qMax(lanes, qMax(int(entering.size()), int(active.size())));
            graph.append(row);
        }
        endResetModel();
    }
};

namespace {
class GraphDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        const auto *model = static_cast<const GitGraphModel *>(index.model());
        if (index.row() >= model->graph.size()) return;
        const auto &row = model->graph[index.row()];
        painter->save(); painter->setClipRect(option.rect); painter->setRenderHint(QPainter::Antialiasing);
        const auto x = [&](int lane) { return option.rect.left() + 16.0 + lane * 18.0; };
        const double center = option.rect.center().y() + .5;
        for (const auto &edge : row.edges) {
            if (edge.to < 0) continue;
            const double top = edge.upper ? option.rect.top() : center;
            const double bottom = edge.upper ? center : option.rect.bottom() + 1;
            QPainterPath path; path.moveTo(x(edge.from), top);
            path.cubicTo(x(edge.from), (top+bottom)/2, x(edge.to), (top+bottom)/2, x(edge.to), bottom);
            painter->setPen(QPen(laneColor(edge.color), 1.6)); painter->drawPath(path);
        }
        painter->setPen(QPen(laneColor(row.color), 1.8));
        painter->setBrush(model->commits[index.row()].parents.size() > 1 ? QColor(UiTheme::Surface) : laneColor(row.color));
        painter->drawEllipse(QPointF(x(row.lane), center), 4.0, 4.0);
        painter->restore();
    }
};
}

GitHistoryWindow::GitHistoryWindow(const QString &projectName, const RepositoryTarget &target, QWidget *parent)
    : QDialog(parent, Qt::Window), target_(target), historyReader_(this), detailReader_(this), fileReader_(this), diffReader_(this), compareReader_(this)
{
    setObjectName(QStringLiteral("gitHistoryWindow"));
    setAttribute(Qt::WA_DeleteOnClose); setWindowFlag(Qt::WindowMaximizeButtonHint);
    setWindowTitle(projectName + QStringLiteral(" · Git 历史 · Orchestrate"));
    resize(1180, 800); setMinimumSize(780, 540);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(20,16,20,16); layout->setSpacing(12);
    auto *environment = plainLabel(this); environment->setText(target.description()); UiTheme::setRole(environment, "muted"); layout->addWidget(environment);
    tabs_ = new QTabWidget(this); layout->addWidget(tabs_, 1);
    auto *historyPage = new QWidget(tabs_); auto *historyLayout = new QVBoxLayout(historyPage);
    historyLayout->setContentsMargins(0,12,0,0);
    auto *toolbar = new QHBoxLayout;
    branch_ = new QComboBox(historyPage); branch_->setObjectName(QStringLiteral("gitBranch")); branch_->setAccessibleName(QStringLiteral("提交范围"));
    branch_->setMinimumContentsLength(16); branch_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    branch_->addItem(QStringLiteral("所有分支与标签"), QString());
    search_ = new QLineEdit(historyPage); search_->setPlaceholderText(QStringLiteral("查找已加载的提交、作者或编号")); search_->setAccessibleName(search_->placeholderText()); search_->setClearButtonEnabled(true);
    auto *find = new QPushButton(QStringLiteral("查找下一个"), historyPage); UiTheme::setRole(find, "quiet");
    refresh_ = new QPushButton(QStringLiteral("刷新"), historyPage); refresh_->setObjectName(QStringLiteral("gitRefresh"));
    auto *detailsToggle = new QCheckBox(QStringLiteral("提交详情"), historyPage); detailsToggle->setChecked(true);
    toolbar->addWidget(branch_); toolbar->addWidget(search_, 1); toolbar->addWidget(find); toolbar->addWidget(detailsToggle); toolbar->addWidget(refresh_); historyLayout->addLayout(toolbar);
    auto *split = new QSplitter(Qt::Vertical, historyPage); split->setChildrenCollapsible(false); historyLayout->addWidget(split, 1);
    model_ = new GitGraphModel(this);
    history_ = new QTableView(split); history_->setObjectName(QStringLiteral("gitCommits")); tableStyle(history_); history_->setModel(model_);
    history_->setItemDelegateForColumn(0, new GraphDelegate(history_)); history_->setAccessibleName(QStringLiteral("Git 提交关系图与历史"));
    history_->setColumnWidth(0,100); history_->setColumnWidth(1,330); history_->setColumnWidth(2,180); history_->setColumnWidth(3,100); history_->setColumnWidth(4,155); history_->setColumnWidth(5,100);
    auto *detail = new QWidget(split); auto *detailLayout = new QVBoxLayout(detail); detailLayout->setContentsMargins(0,6,0,0);
    message_ = new QPlainTextEdit(detail); message_->setObjectName(QStringLiteral("gitCommitMessage")); message_->setReadOnly(true); message_->setMaximumHeight(110); message_->setPlaceholderText(QStringLiteral("选择提交查看完整说明"));
    detailLayout->addWidget(message_);
    auto *parentRow = new QHBoxLayout;
    parentRow->addWidget(new QLabel(QStringLiteral("对比基准"), detail));
    parent_ = new QComboBox(detail); parent_->setAccessibleName(QStringLiteral("对比父提交")); parentRow->addWidget(parent_);
    detailStatus_ = plainLabel(detail); UiTheme::setRole(detailStatus_, "muted"); parentRow->addWidget(detailStatus_,1); detailLayout->addLayout(parentRow);
    auto *fileSplit = new QSplitter(detail); fileSplit->setChildrenCollapsible(false);
    files_ = new QListWidget(fileSplit); files_->setObjectName(QStringLiteral("gitFiles")); files_->setAccessibleName(QStringLiteral("变更文件")); files_->setMinimumWidth(150);
    diff_ = new QPlainTextEdit(fileSplit); diff_->setObjectName(QStringLiteral("gitDiff")); diff_->setReadOnly(true); diff_->setLineWrapMode(QPlainTextEdit::NoWrap); diff_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    diff_->setPlaceholderText(QStringLiteral("选择文件查看差异")); new DiffHighlighter(diff_->document());
    fileSplit->setSizes({250,800}); detailLayout->addWidget(fileSplit,1); split->setSizes({350,350});
    auto *footer = new QHBoxLayout;
    status_ = plainLabel(historyPage); status_->setObjectName(QStringLiteral("gitStatus")); footer->addWidget(status_,1);
    more_ = new QPushButton(QStringLiteral("继续加载"),historyPage); more_->setObjectName(QStringLiteral("gitLoadMore")); more_->setEnabled(false); footer->addWidget(more_); historyLayout->addLayout(footer);
    tabs_->addTab(historyPage, QStringLiteral("提交历史"));
    auto *comparisonPage = new QWidget(tabs_); auto *comparisonLayout = new QVBoxLayout(comparisonPage); comparisonLayout->setContentsMargins(0,12,0,0);
    auto *compareToolbar = new QHBoxLayout;
    remote_ = new QComboBox(comparisonPage); remote_->setAccessibleName(QStringLiteral("对比远端")); remote_->setMinimumWidth(140);
    branchSearch_ = new QLineEdit(comparisonPage); branchSearch_->setPlaceholderText(QStringLiteral("搜索分支名称")); branchSearch_->setAccessibleName(branchSearch_->placeholderText()); branchSearch_->setClearButtonEnabled(true);
    differences_ = new QCheckBox(QStringLiteral("只看差异"), comparisonPage); differences_->setChecked(true);
    compareRefresh_ = new QPushButton(QStringLiteral("刷新对比"),comparisonPage); compareRefresh_->setObjectName(QStringLiteral("gitCompareRefresh"));
    compareToolbar->addWidget(remote_); compareToolbar->addWidget(branchSearch_,1); compareToolbar->addWidget(differences_); compareToolbar->addWidget(compareRefresh_); comparisonLayout->addLayout(compareToolbar);
    auto *meaning = plainLabel(comparisonPage); meaning->setText(QStringLiteral("本地指当前绑定的仓库。同名分支按最新提交编号比较；不一致不表示领先或落后。")); UiTheme::setRole(meaning,"muted"); comparisonLayout->addWidget(meaning);
    branches_ = new QTableWidget(0,4,comparisonPage); branches_->setObjectName(QStringLiteral("gitBranchComparison")); tableStyle(branches_);
    branches_->setHorizontalHeaderLabels({QStringLiteral("分支"),QStringLiteral("状态"),QStringLiteral("本地提交"),QStringLiteral("远端提交")});
    branches_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); branches_->setColumnWidth(1,150); branches_->setColumnWidth(2,200); branches_->setColumnWidth(3,200); comparisonLayout->addWidget(branches_,1);
    compareStatus_ = plainLabel(comparisonPage); compareStatus_->setObjectName(QStringLiteral("gitCompareStatus")); comparisonLayout->addWidget(compareStatus_);
    tabs_->addTab(comparisonPage,QStringLiteral("分支对比"));
    for (auto *button : findChildren<QPushButton *>()) button->setAutoDefault(false);
    connect(refresh_,&QPushButton::clicked,this,&GitHistoryWindow::refreshHistory);
    connect(more_,&QPushButton::clicked,this,[this]{ loadPage(true,refs_,head_); });
    connect(branch_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{ loadPage(false,refs_,head_); });
    connect(history_->selectionModel(),&QItemSelectionModel::currentRowChanged,this,[this]{ selectCommit(); });
    connect(parent_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{ loadFiles(); });
    connect(files_,&QListWidget::currentRowChanged,this,[this]{ loadDiff(); });
    connect(detailsToggle,&QCheckBox::toggled,detail,&QWidget::setVisible);
    connect(find,&QPushButton::clicked,this,&GitHistoryWindow::findNext);
    connect(search_,&QLineEdit::returnPressed,this,&GitHistoryWindow::findNext);
    connect(tabs_,&QTabWidget::currentChanged,this,[this](int index){ if (index == 1 && !compareLoaded_) loadRemotes(); });
    connect(compareRefresh_,&QPushButton::clicked,this,&GitHistoryWindow::loadRemotes);
    connect(remote_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{ compare(); });
    connect(branchSearch_,&QLineEdit::textChanged,this,[this]{ displayBranches(); });
    connect(differences_,&QCheckBox::toggled,this,[this]{ displayBranches(); });
    QTimer::singleShot(0,this,&GitHistoryWindow::refreshHistory);
}
GitHistoryWindow::~GitHistoryWindow() = default;
void GitHistoryWindow::done(int result) {
    historyReader_.cancel(); detailReader_.cancel(); fileReader_.cancel(); diffReader_.cancel(); compareReader_.cancel();
    QDialog::done(result);
}
void GitHistoryWindow::historyStatus(const QString &message, bool error) {
    status_->setText(message + (historyTime_.isValid() ? QStringLiteral("\n上次读取：") + historyTime_.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString()));
    UiTheme::setRole(status_,error ? "error" : "muted");
}
void GitHistoryWindow::refreshHistory()
{
    historyBusy_ = true; refresh_->setEnabled(false); more_->setEnabled(false); branch_->setEnabled(false); historyStatus(QStringLiteral("正在读取仓库历史…"));
    const auto failure = [this](const QString &error) { historyBusy_ = false; refresh_->setEnabled(true); branch_->setEnabled(true); more_->setEnabled(hasMore_); historyStatus(error,true); };
    historyReader_.run(target_,{QStringLiteral("rev-parse"),QStringLiteral("--git-dir")},[this,failure](Git::Result result){
        if (!result.ok) { failure(QStringLiteral("无法读取该目录的 Git 仓库。\n") + result.error); return; }
        historyReader_.run(target_,{QStringLiteral("for-each-ref"),RefFormat,QStringLiteral("refs/heads"),QStringLiteral("refs/remotes"),QStringLiteral("refs/tags")},[this,failure](Git::Result result){
            QList<Git::Reference> refs;
            if (!result.ok || !Git::parseReferences(result.output,&refs)) { failure(result.ok ? QStringLiteral("无法解析仓库引用。") : result.error); return; }
            historyReader_.run(target_,{QStringLiteral("rev-parse"),QStringLiteral("--verify"),QStringLiteral("--quiet"),QStringLiteral("HEAD")},[this,refs,failure](Git::Result result){
                const auto head = QString::fromLatin1(result.output).trimmed();
                if ((!result.ok && result.exitCode != 1) || (result.ok && !Git::validObjectId(head))) { failure(result.error.isEmpty() ? QStringLiteral("无法解析 HEAD。") : result.error); return; }
                loadPage(false,refs,result.ok ? head : QString());
            });
        },0);
    });
}
void GitHistoryWindow::loadPage(bool append, const QList<Git::Reference> &refs, const QString &head)
{
    const QString filter = branch_->currentData().toString();
    QStringList tips;
    if (filter.isEmpty()) { for (const auto &ref : refs) if (!tips.contains(ref.id)) tips.append(ref.id); if (!head.isEmpty() && !tips.contains(head)) tips.append(head); }
    else if (filter == QStringLiteral("HEAD")) { if (!head.isEmpty()) tips.append(head); }
    else for (const auto &ref : refs) if (ref.name == filter) tips.append(ref.id);
    historyBusy_ = true; refresh_->setEnabled(false); branch_->setEnabled(false); more_->setEnabled(false); historyStatus(QStringLiteral("正在读取提交…"));
    const int requested = !append && filter == loadedBranch_ ? qMax(PageSize,int(model_->commits.size())) : PageSize;
    const auto receive = [this,append,refs,head,filter,requested](Git::Result result){
        historyBusy_ = false; refresh_->setEnabled(true); branch_->setEnabled(true);
        QList<Git::Commit> page;
        if (!result.ok || !Git::parseCommits(result.output,&page)) {
            more_->setEnabled(hasMore_); historyStatus(result.ok ? QStringLiteral("提交数据不完整，请重试。") : result.error,true);
            const QSignalBlocker block(branch_); branch_->setCurrentIndex(qMax(0,branch_->findData(loadedBranch_))); return;
        }
        const QString selected = selected_.id;
        const bool next = page.size() > requested; if (next) page.removeLast();
        if (append) page = model_->commits + page;
        refs_ = refs; head_ = head; loadedBranch_ = filter; hasMore_ = next; historyTime_ = QDateTime::currentDateTimeUtc();
        { const QSignalBlocker block(branch_); branch_->clear(); branch_->addItem(QStringLiteral("所有分支与标签"),QString()); branch_->addItem(QStringLiteral("当前 HEAD"),QStringLiteral("HEAD"));
          for (const auto &ref : refs) branch_->addItem(ref.name.mid(5),ref.name);
          int index = branch_->findData(filter); if (index < 0) { branch_->addItem(QStringLiteral("已移除：") + filter,filter); index = branch_->count()-1; } branch_->setCurrentIndex(index); }
        model_->replace(page,refs,head); history_->setColumnWidth(0,qMax(90,model_->lanes*18+32));
        more_->setEnabled(hasMore_);
        historyStatus(page.isEmpty() ? (filter.isEmpty() ? QStringLiteral("尚无提交。") : QStringLiteral("此范围没有提交。")) : QStringLiteral("已加载 %1 条提交%2").arg(page.size()).arg(hasMore_ ? QString() : QStringLiteral(" · 已全部加载")));
        int row = 0; for (int i=0;i<page.size();++i) if (page[i].id == selected) { row=i; break; }
        if (!page.isEmpty()) history_->setCurrentIndex(model_->index(row,1)); else selectCommit();
    };
    if (tips.isEmpty()) { historyReader_.cancel(); receive({true,{},{},0}); return; }
    historyReader_.run(target_,Git::logArguments(append ? model_->commits.size() : 0,requested+1),receive,16*1024*1024,(tips.join('\n')+QLatin1Char('\n')).toLatin1());
}
void GitHistoryWindow::selectCommit()
{
    detailReader_.cancel(); fileReader_.cancel(); diffReader_.cancel();
    selected_ = {}; changes_.clear(); message_->clear(); files_->clear(); diff_->clear(); detailStatus_->clear();
    const QSignalBlocker block(parent_); parent_->clear();
    const int row = history_->currentIndex().row(); if (row < 0 || row >= model_->commits.size()) return;
    selected_ = model_->commits[row];
    if (selected_.parents.isEmpty()) parent_->addItem(QStringLiteral("空树（首次提交）"),QString());
    for (int i=0;i<selected_.parents.size();++i) parent_->addItem(QStringLiteral("父提交 %1 · %2").arg(i+1).arg(selected_.parents[i].left(10)),selected_.parents[i]);
    message_->setPlainText(selected_.id + QStringLiteral("\n") + selected_.subject);
    detailReader_.run(target_,{QStringLiteral("show"),QStringLiteral("-s"),QStringLiteral("--format=%B"),selected_.id,QStringLiteral("--")},[this](Git::Result result){
        if (!result.ok) { message_->setPlainText(selected_.id+QLatin1Char('\n')+selected_.subject+QLatin1Char('\n')+result.error); return; }
        message_->setPlainText(selected_.id + QStringLiteral("\n") + selected_.author + QStringLiteral(" · ") + selected_.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral("\n\n") + QString::fromUtf8(result.output));
    });
    loadFiles();
}
void GitHistoryWindow::loadFiles()
{
    fileReader_.cancel(); diffReader_.cancel(); changes_.clear(); files_->clear(); diff_->clear();
    if (selected_.id.isEmpty()) return;
    detailStatus_->setText(QStringLiteral("正在读取变更文件…"));
    auto args = Git::fileArguments(selected_.id,parent_->currentData().toString()); args << QStringLiteral("--");
    fileReader_.run(target_,args,[this](Git::Result result){
        if (!result.ok || !Git::parseFiles(result.output,&changes_)) { detailStatus_->setText(result.ok ? QStringLiteral("无法解析变更文件。") : result.error); return; }
        for (const auto &file : changes_) {
            auto *item = new QListWidgetItem(file.status + QStringLiteral("  ") + (file.oldPath.isEmpty() ? file.path : file.oldPath + QStringLiteral(" → ") + file.path),files_);
            item->setToolTip(item->text());
        }
        detailStatus_->setText(changes_.isEmpty() ? QStringLiteral("相对所选基准没有文件变更。") : QStringLiteral("%1 个变更文件").arg(changes_.size()));
        if (!changes_.isEmpty()) files_->setCurrentRow(0);
    });
}
void GitHistoryWindow::loadDiff()
{
    diffReader_.cancel(); diff_->clear();
    const int row = files_->currentRow(); if (row < 0 || row >= changes_.size()) return;
    diff_->setPlainText(QStringLiteral("正在读取差异…"));
    const auto file = changes_[row];
    auto args = Git::fileArguments(selected_.id,parent_->currentData().toString(),true); args << QStringLiteral("--");
    if (!file.oldPath.isEmpty()) args << Git::literalFilePath(file.oldPath);
    args << Git::literalFilePath(file.path);
    diffReader_.run(target_,args,[this](Git::Result result){ diff_->setPlainText(result.ok ? QString::fromUtf8(result.output) : result.error); },4*1024*1024);
}
void GitHistoryWindow::loadRemotes()
{
    compareRefresh_->setEnabled(false); remote_->setEnabled(false); compareStatus_->setText(QStringLiteral("正在读取远端配置…"));
    compareReader_.run(target_,{QStringLiteral("remote")},[this](Git::Result result){
        compareRefresh_->setEnabled(true); remote_->setEnabled(true);
        if (!result.ok) { comparisonError_=result.error; displayBranches(); return; }
        const QString previous = remote_->currentText();
        { const QSignalBlocker block(remote_); remote_->clear(); remote_->addItems(QString::fromUtf8(result.output).split('\n',Qt::SkipEmptyParts));
          const int preferred = remote_->findText(previous.isEmpty() ? QStringLiteral("origin") : previous); if (preferred >= 0) remote_->setCurrentIndex(preferred); }
        compareLoaded_ = true;
        if (!remote_->count()) { comparison_.clear(); comparedRemote_.clear(); comparisonError_.clear(); compareTime_ = {}; displayBranches(); compareStatus_->setText(QStringLiteral("该仓库未配置远端。")); return; }
        compare();
    },0);
}
void GitHistoryWindow::compare()
{
    const auto remote = remote_->currentText(); if (remote.isEmpty()) return;
    compareRefresh_->setEnabled(false); remote_->setEnabled(false); compareStatus_->setText(QStringLiteral("正在联网查询 %1 的全部分支…").arg(remote));
    const auto fail = [this](const QString &error) {
        compareRefresh_->setEnabled(true); remote_->setEnabled(true);
        if (!comparedRemote_.isEmpty()) { const QSignalBlocker block(remote_); const int i=remote_->findText(comparedRemote_); if(i>=0)remote_->setCurrentIndex(i); }
        comparisonError_=error;
        displayBranches();
    };
    compareReader_.run(target_,{QStringLiteral("for-each-ref"),RefFormat,QStringLiteral("refs/heads")},[this,remote,fail](Git::Result result){
        QList<Git::Reference> refs;
        if (!result.ok || !Git::parseReferences(result.output,&refs)) { fail(result.ok ? QStringLiteral("无法解析本地分支。") : result.error); return; }
        QMap<QString,QString> local; for (const auto &ref : refs) local.insert(ref.name.mid(11),ref.id);
        // --heads is compatible with Git versions predating --branches. No output cap.
        compareReader_.run(target_,{QStringLiteral("ls-remote"),QStringLiteral("--heads"),QStringLiteral("--refs"),QStringLiteral("--"),remote},[this,remote,local,fail](Git::Result result){
            QMap<QString,QString> remoteBranches;
            if (!result.ok || !Git::parseRemoteBranches(result.output,&remoteBranches)) { fail(result.ok ? QStringLiteral("远端分支数据不完整，未更新对比结果。") : result.error); return; }
            comparison_ = Git::compareBranches(local,remoteBranches); comparedRemote_ = remote; compareTime_ = QDateTime::currentDateTimeUtc(); comparisonError_.clear();
            compareRefresh_->setEnabled(true); remote_->setEnabled(true); UiTheme::setRole(compareStatus_,"muted"); displayBranches();
        },0);
    },0);
}
void GitHistoryWindow::displayBranches()
{
    branches_->setRowCount(0); int different = 0;
    for (const auto &branch : comparison_) {
        if (branch.state != Git::BranchState::Equal) ++different;
        if ((differences_->isChecked() && branch.state == Git::BranchState::Equal) || !branch.name.contains(branchSearch_->text(),Qt::CaseInsensitive)) continue;
        const int row = branches_->rowCount(); branches_->insertRow(row);
        const QStringList texts{branch.name,Git::branchStateText(branch.state),branch.localId.isEmpty() ? QStringLiteral("—") : branch.localId.left(12),branch.remoteId.isEmpty() ? QStringLiteral("—") : branch.remoteId.left(12)};
        for(int col=0;col<4;++col) { auto *item=new QTableWidgetItem(texts[col]); item->setToolTip(col==2 ? branch.localId : col==3 ? branch.remoteId : texts[col]); branches_->setItem(row,col,item); }
        branches_->item(row,1)->setForeground(QColor(branch.state==Git::BranchState::Equal ? UiTheme::Success : branch.state==Git::BranchState::Different ? UiTheme::Warning : UiTheme::Accent));
    }
    if (!comparisonError_.isEmpty()) {
        compareStatus_->setText(comparisonError_ + (compareTime_.isValid() ? QStringLiteral("\n保留 %1 上次结果 · %2").arg(comparedRemote_,compareTime_.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))) : QString()));
        UiTheme::setRole(compareStatus_,"error");
    } else if (compareTime_.isValid()) compareStatus_->setText(QStringLiteral("%1 · 共 %2 个分支，%3 个有差异 · 显示 %4 个\n读取时间：%5").arg(comparedRemote_).arg(comparison_.size()).arg(different).arg(branches_->rowCount()).arg(compareTime_.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
}
void GitHistoryWindow::findNext()
{
    const auto query = search_->text().trimmed(); if (query.isEmpty() || model_->commits.isEmpty()) return;
    const int count = model_->commits.size(); const int current = history_->currentIndex().row();
    for (int step=1;step<=count;++step) {
        const int row=(current+step+count)%count; const auto &c=model_->commits[row];
        if ((c.id+QLatin1Char('\n')+c.subject+QLatin1Char('\n')+c.author+QLatin1Char('\n')+model_->labels.value(c.id).join(' ')).contains(query,Qt::CaseInsensitive)) {
            history_->setCurrentIndex(model_->index(row,1)); history_->scrollTo(model_->index(row,1)); return;
        }
    }
    historyStatus(QStringLiteral("已加载的提交中没有匹配项。%1").arg(hasMore_ ? QStringLiteral("可继续加载后查找。") : QString()));
}
