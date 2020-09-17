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
    bool sequential;
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
    d->manual_mode_code = -1; // parallel reading
    d->timer = NULL;
    d->context = NULL;
}

QuMultiReader::~QuMultiReader()
{
    if(d->context)
        delete d->context;
    delete d;
}

void QuMultiReader::init(Cumbia *cumbia, const CuControlsReaderFactoryI &r_fac, int manual_mode_code)
{
    d->context = new CuContext(cumbia, r_fac);
    d->manual_mode_code = manual_mode_code;
    d->sequential = d->manual_mode_code >= 0;
}

void QuMultiReader::init(CumbiaPool *cumbia_pool, const CuControlsFactoryPool &fpool, int manual_mode_code)
{
    d->context = new CuContext(cumbia_pool, fpool);
    d->manual_mode_code = manual_mode_code;
    d->sequential = (d->manual_mode_code >= 0);
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

/** \brief inserts src at index position i in the list. If i <= 0, src is prepended to the list. If i >= size(),
 * src is appended to the list, whether or not src is already there.
 *
 * @see setSources
 */
void QuMultiReader::insertSource(const QString &src, int i)
{
    printf("\e[1;35mQuMultiReader.insertSource %s --> %d\e[0m\n", qstoc(src), i);
    CuData options;
    if(d->sequential)  {
        options["period"] = 1000; // d->period;
        options["refresh_mode"] = d->manual_mode_code;
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

void QuMultiReader::removeSource(const QString &src)
{
    if(d->context)
        d->context->disposeReader(src.toStdString());
    d->idx_src_map.remove(d->idx_src_map.key(src));
    d->readersMap.remove(src);
}

/** \brief returns a reference to this object, so that it can be used as a QObject
 *         to benefit from signal/slot connections.
 *
 */
const QObject *QuMultiReader::get_qobject() const
{
    return this;
}

QStringList QuMultiReader::sources() const
{
    return d->idx_src_map.values();
}

int QuMultiReader::period() const
{
    return d->period;
}

void QuMultiReader::setPeriod(int ms) {
    d->period = ms;
    if(!d->sequential)
    {
        CuData per("period", d->period);
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

void QuMultiReader::startRead()
{
    if(d->idx_src_map.size() > 0) {
        // first: returns a reference to the first value in the map, that is the value mapped to the smallest key.
        // This function assumes that the map is not empty.
        const QString& src0 = d->idx_src_map.first();
        d->readersMap[src0]->sendData(CuData("read", ""));
        printf("QuMultiReader.startRead: started cycle with read command for %s...\n", qstoc(d->idx_src_map.first()));
    }
}

void QuMultiReader::m_timerSetup()
{
    if(!d->timer) {
        d->timer = new QTimer(this);
        connect(d->timer, SIGNAL(timeout()), this, SLOT(startRead()));
        d->timer->setSingleShot(true);
        d->timer->setInterval(d->period);
    }
}

void QuMultiReader::onUpdate(const CuData &data)
{
    QString from = QString::fromStdString( data["src"].toString());
    int pos = d->idx_src_map.key(from);
    if(!d->idx_src_map.values().contains(from))
        printf("\e[1;31mQuMultiReader::onUpdate idx_src_map DOES NOT CONTAIN \"%s\"\e[0m\n\n", qstoc(from));
    emit onNewData(data);

    printf("QuMultiReader::onUpdate index \e[1;31m%d\e[0m -> \e[1;33m%s\e[0m\n", pos, datos(data));
    if(d->sequential) {
        bool update = d->databuf.contains(pos);
        d->databuf[pos] = data; // update or new
        if(!update) {
            //#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
            //        QSet<int> res_idx(d->databuf.keys().begin(), d->databuf.keys().end());
            //        QSet<int> idxs(d->idx_src_map.keys().begin(), d->idx_src_map.keys().end());
            //#else
            QSet<int> res_idx = d->databuf.keys().toSet();
            QSet<int> idxs = d->idx_src_map.keys().toSet();
            //#endif
            if(res_idx != idxs) {
                QSet<int> diff = idxs - res_idx;
                QSet<int>::iterator minit = std::min_element(diff.begin(), diff.end());
                qDebug() << __PRETTY_FUNCTION__ << "update for key" << pos << "from" << from << "type" << data["type"].toString().c_str() << res_idx << idxs << diff << *minit;
                if(minit != diff.end()) {
                    int i = *minit;
                    d->readersMap[d->idx_src_map[i]]->sendData(CuData("read", "").set("src", from.toStdString()));
                }
            }
            else { // databuf complete
                emit onSeqReadComplete(d->databuf.values()); // Returns all the values in the map, in ascending order of their keys
                d->databuf.clear();
                printf("\e[1;36m+++++++++++++ \e[1;32mread cycle complete, restarting timer, timeout %d\e[0m\n", d->timer->interval());
                d->timer->start(d->period);
            }
        }
    }
}

#if QT_VERSION < 0x050000
Q_EXPORT_PLUGIN2(cumbia-multiread, QuMultiReader)
#endif // QT_VERSION < 0x050000
