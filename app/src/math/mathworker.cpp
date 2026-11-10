#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <cstdio>
#include <memory>
#include "latex.h"
#include "core/formula.h"
#include "platform/qt/graphic_qt.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
bool supported(const QString &code)
{
    if (code.isEmpty() || code.size() > 4096) return false;
    static const QSet<QString> commands = [] {
        const auto names = QStringLiteral(
            "frac dfrac tfrac cfrac binom dbinom tbinom sqrt left right middle "
            "sum prod coprod int iint iiint oint lim limsup liminf min max sup inf "
            "sin cos tan cot sec csc sinh cosh tanh log ln exp det gcd arg deg dim ker Pr "
            "operatorname mathop mathrm mathsf mathtt mathit mathbf mathbb mathcal mathfrak boldsymbol "
            "text textrm textsf texttt textit textbf mbox boxed overline underline underbrace overbrace "
            "hat widehat tilde widetilde bar vec dot ddot acute grave breve check "
            "alpha beta gamma delta epsilon varepsilon zeta eta theta vartheta iota kappa lambda mu nu "
            "xi omicron pi varpi rho varrho sigma varsigma tau upsilon phi varphi chi psi omega "
            "Gamma Delta Theta Lambda Xi Pi Sigma Upsilon Phi Psi Omega "
            "times div cdot ast star circ bullet pm mp le leq ge geq neq ne approx sim simeq equiv "
            "propto in notin ni subset supset subseteq supseteq cup cap land lor neg lnot setminus "
            "forall exists nexists emptyset varnothing infty partial nabla ell hbar Re Im "
            "to gets rightarrow leftarrow leftrightarrow Rightarrow Leftarrow Leftrightarrow "
            "longrightarrow longleftarrow longleftrightarrow Longrightarrow Longleftarrow Longleftrightarrow "
            "mapsto uparrow downarrow updownarrow xrightarrow xleftarrow "
            "ldots cdots vdots ddots dots lbrace rbrace langle rangle lvert rvert lVert rVert vert Vert "
            "lfloor rfloor lceil rceil backslash angle triangle perp parallel mid colon "
            "quad qquad thinspace medspace thickspace negthinspace enspace "
            "displaystyle textstyle scriptstyle scriptscriptstyle limits nolimits "
            "big Big bigg Bigg bigl bigr Bigl Bigr biggl biggr Biggl Biggr "
            "begin end hline cr nonumber notag understack overset underset stackrel phantom vphantom hphantom");
        const auto list = names.split(' ');
        return QSet<QString>(list.begin(), list.end());
    }();
    int depth = 0, operations = 0;
    for (qsizetype i = 0; i < code.size(); ++i) {
        const auto ch = code.at(i);
        if (ch == '\\') {
            const auto start = ++i;
            while (i < code.size() && code.at(i).isLetter() && code.at(i).unicode() < 128) ++i;
            if (i > start) {
                const auto name = code.mid(start, i - start);
                if (!commands.contains(name)) return false;
                if (name == "begin" || name == "end") {
                    static const QRegularExpression environment(
                        QStringLiteral("^\\s*\\{(matrix|pmatrix|bmatrix|Bmatrix|vmatrix|Vmatrix|smallmatrix|aligned|gathered|cases)\\}"));
                    if (!environment.match(code.mid(i)).hasMatch()) return false;
                }
                --i;
            } // An escaped brace is not a group.
            if (++operations > 512) return false;
        } else if (ch == '{') {
            if (++depth > 32) return false;
        } else if (ch == '}') {
            if (--depth < 0) return false;
        } else if (ch == '&' && ++operations > 512) return false;
        else if (ch.unicode() == 0) return false;
    }
    return depth == 0;
}
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    qputenv("QT_QPA_PLATFORM", "windows");
    QGuiApplication app(argc, argv);
    Q_INIT_RESOURCE(math_fonts);
    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 1;
    const auto bytes = input.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024) return 2;
    const auto request = QJsonDocument::fromJson(bytes).object();
    const auto formulas = request.value("formulas").toArray();
    const auto size = request.value("size").toDouble(16);
    if (formulas.size() > 128 || !std::isfinite(size) || size < 6 || size > 192) return 2;
    tex::LaTeX::init(":/orchestrate-math/res");
    qint64 outputBytes = 0;
    for (qsizetype i = 0; i < formulas.size(); ++i) {
        const auto entry = formulas[i].toObject();
        const auto code = entry.value("code").toString();
        QJsonObject result{{"index", int(i)}};
        try {
            if (!supported(code)) throw std::runtime_error("unsupported");
            tex::Formula formula;
            tex::TeXParser parser(false, code.toStdWString(), &formula);
            parser.parse();
            std::unique_ptr<tex::TeXRender> render(tex::TeXRenderBuilder()
                .setStyle(entry.value("display").toBool() ? tex::TexStyle::display : tex::TexStyle::text)
                .setTextSize(float(size)).build(formula));
            const int width = render->getWidth() + 4, height = render->getHeight() + 4;
            // Raster at 3x logical resolution; bound both dimensions and memory.
            if (width < 1 || height < 1 || width > 8192 || height > 2048
                || qint64(width) * height > 1024 * 1024) throw std::runtime_error("oversize");
            QImage image(width * 3, height * 3, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
            painter.scale(3, 3);
            tex::Graphics2D_qt graphics(&painter);
            render->draw(graphics, 2, 2);
            painter.end();
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            result.insert("png", QString::fromLatin1(png.toBase64()));
        } catch (const std::exception &) {
            result.insert("error", true);
        }
        auto line = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
        outputBytes += line.size();
        if (outputBytes > 16 * 1024 * 1024) return 3;
        output.write(line);
        output.flush();
    }
    tex::LaTeX::release();
    return 0;
}
