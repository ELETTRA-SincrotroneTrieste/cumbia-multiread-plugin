#include "qumultireader.h"
#include <cucontext.h>
#include <cucontrolsreader_abs.h>
#include <cudata.h>
#include <QTimer>
#include <QMap>
#include <QtDebug>

class QuMultiReaderPrivate
{
public:
    QMap<QString, CuControlsReaderA* > readersMap;
    bool sequential, manual;
    int period, manual_mode_code;
    CuContext *context;
    QTimer *timer;
    QMap<int, CuData> databuf;
    QMap<int, QString> idx_src_map;
};

QuMultiReader::QuMultiReader(QObject *parent) :
    QObject(parent)
{
    d = new QuMultiReaderPrivate;
    d->sequential = false;
    d->period = 1000;
    d->manual_mode_code = 0; // sequential reading
    d->manual = false;
    d->timer = NULL;
    d->context = NULL;
}

QuMultiReader::~QuMultiReader()
{
    if(d->context)
        delete d->context;
    delete d;
}

void QuMultiReader::init(Cumbia *cumbia, const CuControlsReaderFactoryI &r_fac, int manual_mode_code) {
    d->context = new CuContext(cumbia, r_fac);
    d->manual_mode_code = manual_mode_code;
    d->sequential = d->manual_mode_code >= 0;
}

void QuMultiReader::init(CumbiaPool *cumbia_pool, const CuControlsFactoryPool &fpool, int manual_mode_code) {
    d->context = new CuContext(cumbia_pool, fpool);
    d->manual_mode_code = manual_mode_code;
    d->sequential = (d->manual_mode_code >= 0);
}

void QuMultiReader::sendData(const QString &s, const CuData &da) {
    printf("\e[1;35mQuMultiReader.sendData: sending data to %s\e[0m\n", qstoc(s));
    CuControlsReaderA *r = d->readersMap[s];
    if(r) r->sendData(da);
}

void QuMultiReader::sendData(int index, const CuData &da) {
    const QString& src = d->idx_src_map[index];
    if(!src.isEmpty())
        sendData(src, da);
}

void QuMultiReader::setSources(const QStringList &srcs)
{
    unsetSources();
    for(int i = 0; i < srcs.size(); i++)
        insertSource(srcs[i], i);
}

void QuMultiReader::unsetSources()
{
    d->context->disposeReader(); // empty arg: dispose all
    d->idx_src_map.clear();
    d->readersMap.clear();
}

/** \brief inserts src at index position i in the list. i must be >= 0
 *
 * @see setSources
 */
void QuMultiReader::insertSource(const QString &src, int i) {
    if(i < 0)
        perr("QuMultiReader.insertSource: i must be >= 0");
    else {
        cuprintf("\e[1;35mQuMultiReader.insertSource %s --> %d\e[0m\n", qstoc(src), i);
        CuData options;
        if(d->sequential)  {
            options["manual"] = true;
            d->context->setOptions(options);
        }
        CuControlsReaderA* r = d->context->add_reader(src.toStdString(), this);
        if(r) {
            r->setSource(src); // then use r->source, not src
            d->readersMap.insert(r->source(), r);
            d->idx_src_map.insert(i, r->source());
        }
        if(d->idx_src_map.size() == 1 && d->sequential)
            m_timerSetup();
    }
}

void QuMultiReader::removeSource(const QString &src) {
    if(d->context)
        d->context->disposeReader(src.toStdString());
    d->idx_src_map.remove(d->idx_src_map.key(src));
    d->readersMap.remove(src);
}

/** \brief returns a reference to this object, so that it can be used as a QObject
 *         to benefit from signal/slot connections.
 *
 */
const QObject *QuMultiReader::get_qobject() const {
    return this;
}

QStringList QuMultiReader::sources() const {
    return d->idx_src_map.values();
}

/*!
 * \brief Returns the period used by the multi reader if in *sequential* mode
 * \return The period in milliseconds used by the multi reader timer in *sequential* mode
 *
 * \note A negative period requires a manual update through the startRead *slot*.
 */
int QuMultiReader::period() const {
    return d->period;
}

/*!
 * \brief Change the period, in milliseconds
 * \param ms the new period in milliseconds
 *
 * In sequential mode, a negative period requires a manual call to startRead (*slot*) to
 * trigger an update cycle.
 * If not in sequential mode, a negative period is ignored.
 */
void QuMultiReader::setPeriod(int ms) {
    d->period = ms;
    if(!d->sequential && ms > 0) {
        CuData per("period", ms);
        foreach(CuControlsReaderA *r, d->context->readers())
            r->sendData(per);
    }
}

void QuMultiReader::setSequential(bool seq) {
    d->sequential = seq;
}

bool QuMultiReader::sequential() const {
    return d->sequential;
}

/*!
 * \brief Start a read operation. Used internally if mode is *sequential* and *period* greater than 0
 * Call this explicitly to start a read cycle in *manual mode*, that is *period <= 0*
 */
void QuMultiReader::startRead()
{
    if(d->idx_src_map.size() > 0) {
        // first: returns a reference to the first value in the map, that is the value mapped to the smallest key.
        // This function assumes that the map is not empty.
        const QString& src0 = d->idx_src_map.first();
        d->readersMap[src0]->sendData(CuData("read", ""));
        cuprintf("QuMultiReader.startRead: started cycle with read command for %s...\n", qstoc(d->idx_src_map.first()));
    }
}

void QuMultiReader::m_timerSetup() {
    if(!d->timer) {
        d->timer = new QTimer(this);
        connect(d->timer, SIGNAL(timeout()), this, SLOT(startRead()));
        d->timer->setSingleShot(true);
        if(d->period > 0)
            d->timer->setInterval(d->period);
    }
}

// find the index that matches src, discarding args
int QuMultiReader::m_matchNoArgs(const QString &src) const {
    foreach(int k, d->idx_src_map.keys()) {
        const QString& s = d->idx_src_map[k];
        printf("\e[1;36mQuMultiReader::m_matchNoArgs comparing %s with %s\e[0m\n", qstoc(s), qstoc(src));
        if(s.section('(', 0, 0) == src.section('(', 0, 0))
            return k;
    }
    return -1;
}

void QuMultiReader::onUpdate(const CuData &data) {
    QString from = QString::fromStdString( data["src"].toString());
    int pos;
    const QList<QString> &srcs = d->idx_src_map.values();
    srcs.contains(from) ? pos = d->idx_src_map.key(from) : pos = m_matchNoArgs(from);
    if(pos < 0)
        printf("\e[1;31mQuMultiReader::onUpdate idx_src_map DOES NOT CONTAIN \"%s\"\e[0m\n\n", qstoc(from));
    emit onNewData(data);
    if(d->sequential && pos >= 0) {
        bool update = d->databuf.contains(pos);
        d->databuf[pos] = data; // update or new
        if(!update) {
            //#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
            //        QSet<int> res_idx(d->databuf.keys().begin(), d->databuf.keys().end());
            //        QSet<int> idxs(d->idx_src_map.keys().begin(), d->idx_src_map.keys().end());
            //#else
            const QList<int> &dkeys = d->databuf.keys();
            const QList<int> &idxli = d->idx_src_map.keys();
            const QSet<int>& res_idx = dkeys.toSet();
            const QSet<int>& idxs = idxli.toSet();
            //#endif
            if(res_idx != idxs) {
                QSet<int> diff = idxs - res_idx;
                QSet<int>::iterator minit = std::min_element(diff.begin(), diff.end());
                if(minit != diff.end()) {
                    int i = *minit;
                    const QString& s = d->idx_src_map[i];
                    cuprintf("QuMultiReader::onUpdate: sending read for index %d src %s\n", i, s.toStdString().c_str());
                    d->readersMap[d->idx_src_map[i]]->sendData(CuData("read", "").set("src", s.toStdString()));
                }
            }
            else { // databuf complete
                emit onSeqReadComplete(d->databuf.values()); // Returns all the values in the map, in ascending order of their keys
                d->databuf.clear();

                if(d->period > 0) cuprintf("QuMultiReader.onUpdate: \e[1;32m+\e[0m read cycle complete, restarting timer, timeout %d\n", d->timer->interval());
                else cuprintf("QuMultiReader.onUpdate: \e[1;35mi\e[0m: timeout <= 0: not restarting timer\n");
                if(d->period > 0)
                    d->timer->start(d->period);
            }
        }
    }
}

#if QT_VERSION < 0x050000
Q_EXPORT_PLUGIN2(cumbia-multiread, QuMultiReader)
#endif // QT_VERSION < 0x050000
