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
    int period, mode;
    CuContext *context;
    QTimer *timer;
    QMap<int, CuData> databuf;
    QMap<int, QString> idx_src_map;
};

QuMultiReader::QuMultiReader(QObject *parent) :
    QObject(parent)
{
    d = new QuMultiReaderPrivate;
    d->period = 1000;
    d->mode = SequentialReads; // sequential reading
    d->timer = NULL;
    d->context = NULL;
}

QuMultiReader::~QuMultiReader()
{
    if(d->context)
        delete d->context;
    delete d;
}

void QuMultiReader::init(Cumbia *cumbia, const CuControlsReaderFactoryI &r_fac, int mode) {
    d->context = new CuContext(cumbia, r_fac);
    d->mode = mode;
    if(d->mode >= SequentialManual) d->period = -1;
}

void QuMultiReader::init(CumbiaPool *cumbia_pool, const CuControlsFactoryPool &fpool, int mode) {
    d->context = new CuContext(cumbia_pool, fpool);
    d->mode = mode;
    if(d->mode >= SequentialManual) d->period = -1;
}

void QuMultiReader::sendData(const QString &s, const CuData &da) {
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
        if(d->mode >= SequentialManual) {
            options["manual"] = true;
        }
        else if(d->mode == SequentialReads && d->period > 0)  {
            // readings in the same thread
            options["refresh_mode"] = 1; // CuTReader::PolledRefresh
            options["period"] = d->period;
        }
        if(d->mode >= SequentialReads) // manual or seq
            options["thread_token"] = QString("multi_reader_%1").arg(objectName()).toStdString();
        d->context->setOptions(options);
        printf("QuMultiReader.insertSource: options passed: %s\n", datos(options));
    }
    CuControlsReaderA* r = d->context->add_reader(src.toStdString(), this);
    if(r) {
        r->setSource(src); // then use r->source, not src
        d->readersMap.insert(r->source(), r);
        d->idx_src_map.insert(i, r->source());
    }
    if(d->idx_src_map.size() == 1 && d->mode == SequentialReads)
        m_timerSetup();

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
    if(d->mode == SequentialReads && ms > 0) {
        CuData per("period", ms);
        per["refresh_mode"] = 1;
        foreach(CuControlsReaderA *r, d->context->readers())
            r->sendData(per);
    }
}

void QuMultiReader::setSequential(bool seq) {
    seq ? d->mode = SequentialReads : d->mode = ConcurrentReads;
}

bool QuMultiReader::sequential() const {
    return d->mode >= SequentialReads;
}

void QuMultiReader::startRead() {
    if(d->idx_src_map.size() > 0) {
        // first: returns a reference to the first value in the map, that is the value mapped to the smallest key.
        // This function assumes that the map is not empty.
        const QString& src0 = d->idx_src_map.first();
        d->readersMap[src0]->sendData(CuData("read", ""));
        cuprintf("QuMultiReader.startRead: started cycle with read command for %s...\n", qstoc(d->idx_src_map.first()));
    }
}

void QuMultiReader::m_timerSetup() {
    printf("\e[1;31mQuMultiReader.mTimerSetup period: %d\e[0m\n", d->period);
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
    printf("\e[1;31mQuMultiReader.m_matchNoArgs\e[0m\n");
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
    emit onNewData(data);
    if(pos >= 0) {
        d->databuf[pos] = data; // update or new
        // complete data update when a single value changes may be handy in concurrent mode
        emit onNewData(d->databuf.values());
        if((d->mode >= SequentialReads)) {
            const QList<int> &dkeys = d->databuf.keys();
            const QList<int> &idxli = d->idx_src_map.keys();
            if(dkeys == idxli) { // databuf complete
                emit onSeqReadComplete(d->databuf.values()); // Returns all the values in the map, in ascending order of their keys
                d->databuf.clear();
            }
        }
    }
}

QuMultiReaderPluginInterface *QuMultiReader::getMultiSequentialReader(QObject *parent, bool manual_refresh) {
    QuMultiReader *r = nullptr;
    if(!d->context)
        perr("QuMultiReader.getMultiSequentialReader: call QuMultiReader.init before getMultiSequentialReader");
    else {
        r = new QuMultiReader(parent);
        r->init(d->context->cumbiaPool(), d->context->getControlsFactoryPool(), manual_refresh ? SequentialManual : SequentialReads);
    }
    return r;
}

QuMultiReaderPluginInterface *QuMultiReader::getMultiConcurrentReader(QObject *parent) {
    QuMultiReader *r = nullptr;
    if(!d->context)
        perr("QuMultiReader.getMultiSequentialReader: call QuMultiReader.init before getMultiSequentialReader");
    else {
        r = new QuMultiReader(parent);
        r->init(d->context->cumbiaPool(), d->context->getControlsFactoryPool(), ConcurrentReads);
    }
    return r;
}

CuContext *QuMultiReader::getContext() const {
    return d->context;
}

#if QT_VERSION < 0x050000
Q_EXPORT_PLUGIN2(cumbia-multiread, QuMultiReader)
#endif // QT_VERSION < 0x050000
