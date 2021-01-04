#ifndef QUMULTIREADER_H
#define QUMULTIREADER_H

#include <QObject>
#include <QList>
#include <qumultireaderplugininterface.h>
#include <cudata.h>
#include <cudatalistener.h>

class QuMultiReaderPrivate;
class Cumbia;
class CumbiaPool;
class CuControlsReaderFactoryI;
class CuControlsFactoryPool;
class CuControlsReaderA;

/** \mainpage This plugin allows parallel and sequential reading from multiple sources
 *
 * Please read QuMultiReaderPluginInterface documentation and the <em>multireader</em> example
 * under the examples subfolder of the plugin directory.
 *
 * \code
 *
void Multireader::m_loadMultiReaderPlugin()
{
    QObject *plugin_qob;

    if(!plugin) { // initialize plugin once
        plugin = pl.get<QuMultiReaderPluginInterface>("libcumbia-multiread-plugin.so", &plugin_qob);
        if(!plugin)
            perr("Multireader: failed to load plugin \"libcumbia-multiread-plugin.so\"");
        if (plugin) {
            m_multir = qobject_cast<QuMultiReaderPluginInterface *>(plugin);

            // configure multi reader
            // cu_t is a reference to CumbiaTango
            // cu_tango_r_fac is a CuTReaderFactory
            // CuTReader::Manual is the CuTReader::RefreshMode enum value that identifies
            // manual refresh mode for the Tango engine. It tells the QuMultiReader to
            // perform a sequential read of the sources, one after another, and emit the
            // onSeqReadComplete when each cycle is over.
            // A value of -1 instead of CuTReader::Manual would configure the QuMultiReader to
            // read the n sources concurrently. In this case, no onSeqReadComplete is emitted.
            //
            m_multir->init(cu_t, cu_tango_r_fac,  CuTReader::Manual);
            // get multi reader as qobject in order to connect signals to slots
            connect(m_multir->get_qobject(), SIGNAL(onNewData(const CuData&)), this, SLOT(newData(const CuData&)));
            connect(m_multir->get_qobject(), SIGNAL(onSeqReadComplete(const QList<CuData >&)), this, SLOT(seqReadComplete(const QList<CuData >&)));
            // set the sources
            m_multir->setSources(srcs);
        }
        else
            perr("Multireader.m_loadMultiReaderPlugin: error loading plugin: %s", qstoc(pluginLoader.errorString()));
    }
}
 *
 * \endcode
 *
 * \par Warning
 * Do not forget to call
 *
 * \code
 * m_multir->unsetSources
 * \endcode
 *
 * before the application exits. In fact, if the plugin is destroyed *after* Cumbia, the behavior is undefined.
 */
class QuMultiReader : public QObject, public QuMultiReaderPluginInterface, public CuDataListener
{
    Q_OBJECT
#if QT_VERSION >= 0x050000
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QGenericPluginFactoryInterface" FILE "cumbia-multiread.json")
#endif // QT_VERSION >= 0x050000

    Q_INTERFACES(QuMultiReaderPluginInterface)

public:
    QuMultiReader(QObject *parent = 0);

    virtual ~QuMultiReader();

    // QuMultiReaderPluginInterface interface
public:

    void init(Cumbia *cumbia, const CuControlsReaderFactoryI &r_fac, int manual_mode_code = 0);
    void init(CumbiaPool *cumbia_pool, const CuControlsFactoryPool &fpool, int manual_mode_code = 0);
    void sendData(const QString& s, const CuData& da);
    void sendData(int index, const CuData& da);
    void setSources(const QStringList &srcs);
    void unsetSources();
    void insertSource(const QString &src, int i);
    void removeSource(const QString &src);
    const QObject *get_qobject() const;
    QStringList sources() const;

    int period() const;
    void setPeriod(int ms);
    void setSequential(bool seq);
    bool sequential() const;

public slots:
    void startRead();

signals:
    void onNewData(const CuData& da);
    void onSeqReadComplete(const QList<CuData >& data);

private:
    QuMultiReaderPrivate *d;

    void m_timerSetup();
    int m_matchNoArgs(const QString& src) const;

    // CuDataListener interface
public:
    void onUpdate(const CuData &data);
};

#endif // QUMULTIREADER_H
