#include "githistorywindow.h"
#include "gitcommitdialog.h"
#include <QShortcut>
#include "ui/theme.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
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
    : QDialog(parent, Qt::Window), target_(target), historyReader_(this), compareReader_(this)
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
    branch_ = new QComboBox(historyPage); branch_->setObjectName(QStringLiteral("gitBranch")); branch_->setAccessibleName(QStringLiteral("分支"));
    branch_->setPlaceholderText(QStringLiteral("选择分支"));
    branch_->setMinimumContentsLength(10); branch_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    branch_->addItem(QStringLiteral("全部历史"), QString());
    tag_ = new QComboBox(historyPage); tag_->setObjectName(QStringLiteral("gitTag")); tag_->setAccessibleName(QStringLiteral("标签"));
    tag_->setMinimumContentsLength(8); tag_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    tag_->addItem(QStringLiteral("选择标签"), QString());
    search_ = new QLineEdit(historyPage); search_->setPlaceholderText(QStringLiteral("查找已加载的提交、作者或编号")); search_->setAccessibleName(search_->placeholderText()); search_->setClearButtonEnabled(true);
    auto *find = new QPushButton(QStringLiteral("查找下一个"), historyPage); UiTheme::setRole(find, "quiet");
    refresh_ = new QPushButton(QStringLiteral("刷新"), historyPage); refresh_->setObjectName(QStringLiteral("gitRefresh"));
    toolbar->addWidget(new QLabel(QStringLiteral("分支"), historyPage)); toolbar->addWidget(branch_); toolbar->addWidget(new QLabel(QStringLiteral("标签"), historyPage)); toolbar->addWidget(tag_); toolbar->addWidget(search_, 1); toolbar->addWidget(find); toolbar->addWidget(refresh_); historyLayout->addLayout(toolbar);
    model_ = new GitGraphModel(this);
    history_ = new QTableView(historyPage); historyLayout->addWidget(history_,1); history_->setObjectName(QStringLiteral("gitCommits")); tableStyle(history_); history_->setModel(model_);
    history_->setItemDelegateForColumn(0, new GraphDelegate(history_)); history_->setAccessibleName(QStringLiteral("Git 提交关系图与历史"));
    history_->setColumnWidth(0,100); history_->setColumnWidth(1,330); history_->setColumnWidth(2,180); history_->setColumnWidth(3,100); history_->setColumnWidth(4,155); history_->setColumnWidth(5,100);
    history_->setToolTip(QStringLiteral("双击提交或按 Enter 查看详情"));
    auto *footer = new QHBoxLayout;
    status_ = plainLabel(historyPage); status_->setObjectName(QStringLiteral("gitStatus")); footer->addWidget(status_,1);
    more_ = new QPushButton(QStringLiteral("继续加载"),historyPage); more_->setObjectName(QStringLiteral("gitLoadMore")); more_->setVisible(false); more_->setEnabled(false); footer->addWidget(more_); historyLayout->addLayout(footer);
    tabs_->addTab(historyPage, QStringLiteral("提交历史"));
    auto *comparisonPage = new QWidget(tabs_); auto *comparisonLayout = new QVBoxLayout(comparisonPage); comparisonLayout->setContentsMargins(0,12,0,0);
    auto *compareToolbar = new QHBoxLayout;
    remote_ = new QComboBox(comparisonPage); remote_->setAccessibleName(QStringLiteral("对比远端")); remote_->setMinimumWidth(140);
    branchSearch_ = new QLineEdit(comparisonPage); branchSearch_->setPlaceholderText(QStringLiteral("搜索分支名称")); branchSearch_->setAccessibleName(branchSearch_->placeholderText()); branchSearch_->setClearButtonEnabled(true);
    differences_ = new QCheckBox(QStringLiteral("只看差异"), comparisonPage); differences_->setChecked(true);
    compareRefresh_ = new QPushButton(QStringLiteral("刷新对比"),comparisonPage); compareRefresh_->setObjectName(QStringLiteral("gitCompareRefresh"));
    compareToolbar->addWidget(remote_); compareToolbar->addWidget(branchSearch_,1); compareToolbar->addWidget(differences_); compareToolbar->addWidget(compareRefresh_); comparisonLayout->addLayout(compareToolbar);
    branches_ = new QTableWidget(0,4,comparisonPage); branches_->setObjectName(QStringLiteral("gitBranchComparison")); tableStyle(branches_);
    branches_->setToolTip(QStringLiteral("同名分支按最新提交编号比较；不一致不表示领先或落后。"));
    branches_->setHorizontalHeaderLabels({QStringLiteral("分支"),QStringLiteral("状态"),QStringLiteral("本地提交"),QStringLiteral("远端提交")});
    branches_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); branches_->setColumnWidth(1,150); branches_->setColumnWidth(2,200); branches_->setColumnWidth(3,200); comparisonLayout->addWidget(branches_,1);
    compareStatus_ = plainLabel(comparisonPage); compareStatus_->setObjectName(QStringLiteral("gitCompareStatus")); comparisonLayout->addWidget(compareStatus_);
    tabs_->addTab(comparisonPage,QStringLiteral("分支对比"));
    for (auto *button : findChildren<QPushButton *>()) button->setAutoDefault(false);
    connect(refresh_,&QPushButton::clicked,this,&GitHistoryWindow::refreshHistory);
    connect(more_,&QPushButton::clicked,this,[this]{ loadPage(true,refs_,head_); });
    connect(branch_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{ const QSignalBlocker block(tag_); tag_->setCurrentIndex(0); loadPage(false,refs_,head_); });
    connect(tag_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{ const QSignalBlocker block(branch_); branch_->setCurrentIndex(tag_->currentData().toString().isEmpty() ? 0 : -1); loadPage(false,refs_,head_); });
    connect(history_,&QTableView::doubleClicked,this,[this]{ openCommit(); });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto *shortcut = new QShortcut(QKeySequence(key),history_); shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut,&QShortcut::activated,this,&GitHistoryWindow::openCommit);
    }
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
    historyReader_.cancel(); compareReader_.cancel();
    if (detail_) detail_->close();
    QDialog::done(result);
}
void GitHistoryWindow::historyStatus(const QString &message, bool error) {
    status_->setText(message);
    UiTheme::setRole(status_,error ? "error" : "muted");
}
void GitHistoryWindow::refreshHistory()
{
    historyBusy_ = true; refresh_->setEnabled(false); more_->setEnabled(false); branch_->setEnabled(false); tag_->setEnabled(false); historyStatus(QStringLiteral("正在读取仓库历史…"));
    const auto failure = [this](const QString &error) { historyBusy_ = false; refresh_->setEnabled(true); branch_->setEnabled(true); tag_->setEnabled(true); more_->setEnabled(hasMore_); historyStatus(error,true); };
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
    const QString filter = tag_->currentData().toString().isEmpty() ? branch_->currentData().toString() : tag_->currentData().toString();
    QStringList tips;
    if (filter.isEmpty()) { for (const auto &ref : refs) if (!tips.contains(ref.id)) tips.append(ref.id); if (!head.isEmpty() && !tips.contains(head)) tips.append(head); }
    else if (filter == QStringLiteral("HEAD")) { if (!head.isEmpty()) tips.append(head); }
    else for (const auto &ref : refs) if (ref.name == filter) tips.append(ref.id);
    historyBusy_ = true; refresh_->setEnabled(false); branch_->setEnabled(false); tag_->setEnabled(false); more_->setEnabled(false); historyStatus(QStringLiteral("正在读取提交…"));
    const int requested = !append && filter == loadedBranch_ ? qMax(PageSize,int(model_->commits.size())) : PageSize;
    const auto receive = [this,append,refs,head,filter,requested](Git::Result result){
        historyBusy_ = false; refresh_->setEnabled(true); branch_->setEnabled(true); tag_->setEnabled(true);
        QList<Git::Commit> page;
        if (!result.ok || !Git::parseCommits(result.output,&page)) {
            more_->setEnabled(hasMore_); historyStatus(result.ok ? QStringLiteral("提交数据不完整，请重试。") : result.error,true);
            restoreFilter(loadedBranch_); return;
        }
        const int currentRow = history_->currentIndex().row();
        const QString selected = currentRow >= 0 && currentRow < model_->commits.size() ? model_->commits[currentRow].id : QString();
        const bool next = page.size() > requested; if (next) page.removeLast();
        if (append) page = model_->commits + page;
        refs_ = refs; head_ = head; loadedBranch_ = filter; hasMore_ = next;
        { const QSignalBlocker branchBlock(branch_), tagBlock(tag_);
          branch_->clear(); branch_->addItem(QStringLiteral("全部历史"),QString()); branch_->addItem(QStringLiteral("当前 HEAD"),QStringLiteral("HEAD"));
          tag_->clear(); tag_->addItem(QStringLiteral("选择标签"),QString());
          for (const auto &ref : refs) {
              if (ref.name.startsWith(QStringLiteral("refs/tags/"))) tag_->addItem(ref.name.mid(10),ref.name);
              else branch_->addItem(ref.name.startsWith(QStringLiteral("refs/heads/")) ? ref.name.mid(11) : ref.name.mid(13),ref.name);
          }
          restoreFilter(filter); }
        model_->replace(page,refs,head); history_->setColumnWidth(0,qMax(90,model_->lanes*18+32));
        more_->setEnabled(hasMore_); more_->setVisible(hasMore_);
        historyStatus(page.isEmpty() ? (filter.isEmpty() ? QStringLiteral("读取成功 · 尚无提交") : QStringLiteral("读取成功 · 此范围没有提交")) : QStringLiteral("读取成功"));
        int row = 0; for (int i=0;i<page.size();++i) if (page[i].id == selected) { row=i; break; }
        if (!page.isEmpty()) history_->setCurrentIndex(model_->index(row,1));
    };
    if (tips.isEmpty()) { historyReader_.cancel(); receive({true,{},{},0}); return; }
    historyReader_.run(target_,Git::logArguments(append ? model_->commits.size() : 0,requested+1),receive,16*1024*1024,(tips.join('\n')+QLatin1Char('\n')).toLatin1());
}
void GitHistoryWindow::restoreFilter(const QString &filter)
{
    const QSignalBlocker branchBlock(branch_), tagBlock(tag_);
    branch_->setCurrentIndex(filter.startsWith(QStringLiteral("refs/tags/")) ? -1 : 0); tag_->setCurrentIndex(0);
    auto *combo = filter.startsWith(QStringLiteral("refs/tags/")) ? tag_ : branch_;
    int index = combo->findData(filter);
    if (index < 0) { combo->addItem(QStringLiteral("已移除：") + filter,filter); index = combo->count()-1; }
    combo->setCurrentIndex(index);
}
void GitHistoryWindow::openCommit()
{
    const int row = history_->currentIndex().row();
    if (row < 0 || row >= model_->commits.size()) return;
    if (!detail_) detail_ = new GitCommitDialog(target_,this);
    detail_->setCommit(model_->commits[row]);
    if (detail_->isMinimized()) detail_->showNormal(); else detail_->show();
    detail_->raise(); detail_->activateWindow();
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
        if (!remote_->count()) { comparison_.clear(); comparedRemote_.clear(); comparisonError_.clear(); hasComparison_ = false; displayBranches(); compareStatus_->setText(QStringLiteral("该仓库未配置远端。")); return; }
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
            comparison_ = Git::compareBranches(local,remoteBranches); comparedRemote_ = remote; hasComparison_ = true; comparisonError_.clear();
            compareRefresh_->setEnabled(true); remote_->setEnabled(true); UiTheme::setRole(compareStatus_,"muted"); displayBranches();
        },0);
    },0);
}
void GitHistoryWindow::displayBranches()
{
    branches_->setRowCount(0);
    for (const auto &branch : comparison_) {
        if ((differences_->isChecked() && branch.state == Git::BranchState::Equal) || !branch.name.contains(branchSearch_->text(),Qt::CaseInsensitive)) continue;
        const int row = branches_->rowCount(); branches_->insertRow(row);
        const QStringList texts{branch.name,Git::branchStateText(branch.state),branch.localId.isEmpty() ? QStringLiteral("—") : branch.localId.left(12),branch.remoteId.isEmpty() ? QStringLiteral("—") : branch.remoteId.left(12)};
        for(int col=0;col<4;++col) { auto *item=new QTableWidgetItem(texts[col]); item->setToolTip(col==2 ? branch.localId : col==3 ? branch.remoteId : texts[col]); branches_->setItem(row,col,item); }
        branches_->item(row,1)->setForeground(QColor(branch.state==Git::BranchState::Equal ? UiTheme::Success : branch.state==Git::BranchState::Different ? UiTheme::Warning : UiTheme::Accent));
    }
    if (!compareRefresh_->isEnabled()) return;
    if (!comparisonError_.isEmpty()) {
        compareStatus_->setText(comparisonError_ + (hasComparison_ ? QStringLiteral("\n保留 %1 上次成功的结果").arg(comparedRemote_) : QString()));
        UiTheme::setRole(compareStatus_,"error");
    } else if (hasComparison_) {
        compareStatus_->setText(branches_->rowCount() ? QStringLiteral("读取成功") :
            !branchSearch_->text().isEmpty() ? QStringLiteral("读取成功 · 没有匹配的分支") :
            differences_->isChecked() && !comparison_.isEmpty() ? QStringLiteral("读取成功 · 分支一致") : QStringLiteral("读取成功 · 尚无分支"));
        UiTheme::setRole(compareStatus_,"muted");
    }
}
void GitHistoryWindow::findNext()
{
    if (historyBusy_) return;
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
