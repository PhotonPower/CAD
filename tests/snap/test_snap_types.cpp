#include <QTest>
#include <set>

#include "snap/snap_types.hpp"

using nfcad::snap::SnapMask;
using nfcad::snap::SnapType;

class TestSnapTypes : public QObject {
  Q_OBJECT
 private slots:
  void priorityIsTotalOrder() {
    std::set<SnapType> seen(nfcad::snap::kSnapPriority.begin(),
                            nfcad::snap::kSnapPriority.end());
    QCOMPARE(seen.size(), nfcad::snap::kSnapPriority.size());
    QVERIFY(nfcad::snap::priority_rank(SnapType::Point) <
            nfcad::snap::priority_rank(SnapType::End));
    QVERIFY(nfcad::snap::priority_rank(SnapType::End) <
            nfcad::snap::priority_rank(SnapType::Near));
    QCOMPARE(nfcad::snap::priority_rank(SnapType::Grid), 9);
  }

  void defaultMaskExcludesGrid() {
    const SnapMask m = SnapMask::defaults();
    QVERIFY(m.test(SnapType::End));
    QVERIFY(m.test(SnapType::Intersection));
    QVERIFY(!m.test(SnapType::Grid));
  }

  void setAndClear() {
    SnapMask m;
    m.set(SnapType::Mid, true);
    QVERIFY(m.test(SnapType::Mid));
    m.set(SnapType::Mid, false);
    QCOMPARE(m.bits(), std::uint16_t{0});
  }
};

QTEST_GUILESS_MAIN(TestSnapTypes)
#include "test_snap_types.moc"
