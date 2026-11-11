#include <QTest>

#include "kernel/kernel_info.hpp"

class TestKernel : public QObject {
  Q_OBJECT
 private slots:
  void versionIsSet() { QVERIFY(!nfcad::kernel::nurbforge_version().empty()); }
  void selfTest() { QVERIFY(nfcad::kernel::self_test()); }
};

QTEST_GUILESS_MAIN(TestKernel)
#include "test_kernel.moc"
