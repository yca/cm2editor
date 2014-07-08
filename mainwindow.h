#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMap>
#include <QHash>
#include <QMainWindow>

#include <errno.h>

class QListWidgetItem;

/*
 * 13 0C 11 0B 05 0D 07 0E 05 0E 11 10 0B 0D 0D 0F 0C 05 09 0D 10 0C 11 14
 *
 * 13 -> Aggression
 * 0C -> Unknown
 * 11 -> Unknown
 * 0B -> Unknown
 * 05 -> Unknown
 * 0D -> Creativity
 * 07 -> Determination
 * 0E -> Unknown
 * 05 -> Dribbling
 * 0E -> Flair
 * 11 -> Heading
 * 10 -> Influence
 * 0B -> Injury
 * 0D -> Off the ball
 * 0D -> Unknown ??? Maybe adaptibility?
 * 0F -> Pace
 * 0C -> Passing
 * 05 -> Positioning
 * 09 -> Set
 * 0D -> Shooting
 * 10 -> Stamina
 * 0C -> Strength
 * 11 -> Tackling
 * 14 -> Technique
 */
/*
 * 15 154
16 2
17 255
18 255
40 1
75 64
80 6
82 1
129 65
140 1
198 16
199 39

(", Abel Xavier", "Michael, Owen", "Paul, Ince", "Teddy, Lucic", "Philip, Brazier", ", Denilson", "Miklos, Molnar", "Patrik, Berger", ", Nuno Gomes", "Alan, Shearer")
16 2
17 255
18 255
40 1
75 64
80 6
82 1
129 65
140 1
198 16
199 39

(", Abel Xavier", "Michael, Owen", "Paul, Ince", "Teddy, Lucic", "Philip, Brazier", ", Denilson", "Miklos, Molnar", "Patrik, Berger", ", Nuno Gomes", "Alan, Shearer", "Hakan, Sukur")
17 255
18 255
40 1
75 64
80 6
82 1
129 65
140 1
198 16
199 39

("Jari, Litmanen", "Harry, Decheiver", "Mauro, Salvagno", "Olivier, Pickeu", "Paolo, Foglio", "Patrice, Carteron", "Daniele, Daino", "Oleg, Salenko")
17 255
18 255
40 1
75 64
80 6
82 1
129 65
139 1
140 1

("Paul, Scholes", "Ryan, Giggs", "David, Beckham", "Hernán, Crespo", "Marc, Overmars", "Alen, Boksic", "Florian, Maurice", "Roy, Keane", "Gabriel, Batistuta", "Brian, Laudrup")
13 1
17 255
18 255
40 1
75 64
82 1
85 1
86 1
88 1
129 65
140 1
197 100

candidates:

off 16 : country code
let's assume 15-16 is team code


*/
enum PlayerAtr {
	ATT_UNKNOWN_ADAP,
	ATT_AGGRESION,
	ATT_UNKNOWN1,
	ATT_BIGOCC,
	ATT_CHARACTER,
	ATT_CONSIS,
	ATT_CREAT,
	ATT_DETER,
	ATT_DIRTY,
	ATT_DRIB,
	ATT_FLAIR,
	ATT_HEADING,
	ATT_INFLU,
	ATT_INJURY,
	ATT_OFFTHEBALL,
	ATT_UNKNOWN6,
	ATT_PACE,
	ATT_PASSING,
	ATT_POSI,
	ATT_SETP,
	ATT_SHOOTING,
	ATT_STAMINA,
	ATT_STREN,
	ATT_TACK,
	ATT_TECH,

	ATT_COUNT
};

enum PlayerTransferStatus {
	TRST_UNKNOWN,
	TRST_LISTED_BY_CLUB,
	TRST_LISTED_BY_REQUEST,
	TRST_LISTED_FOR_LOAN,
	TRST_ON_A_FREE,
	TRST_UNAVALIABLE,
};

enum PlayerTransferValue {
	TRVAL_FIRM_OFFERs,
	TRVAL_ON_FUTURE,
};

class PlayerData {
public:
	int age;
	uchar bday;
	uchar bmonth;
	uchar byear;
	uchar cyear;
	ushort id;
	uchar atts[ATT_COUNT];
	QString name;
	QString sname;
	char payload[358];
	int potential;
	int ability;
	int reputation;
	PlayerTransferStatus trAvail;
	PlayerTransferValue trValue;
	int teamId;

	enum Positions {
		GK,
		SW,
		DEF,
		ANC,
		MID,
		SUP,
		ATT,

		POS_MAX
	};
	uchar positions[POS_MAX];
	enum Sides {
		LEFT,
		RIGHT,
		CENTRAL
	};
	uchar sides[3];

	int fpos;
	int plpos;

	static QHash<QString, PlayerAtr> attrIndex;
	static bool isAttribute(const QString &attr)
	{
		return attrIndex.contains(attr);
	}
	int attribute(const QString &attr)
	{
		return atts[attrIndex[attr]];
	}
	int setAttribute(const QString &attr, int value)
	{
		if (!isAttribute(attr))
			return -ENOENT;
		atts[attrIndex[attr]] = value;
		return 0;
	}
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = 0);
	~MainWindow();

private slots:
	void on_listPlayers_currentRowChanged(int currentRow);

	void on_lineSearchPlayer_textChanged(const QString &arg1);

	void on_listPlayers_itemDoubleClicked(QListWidgetItem *item);

	void on_actionLeague_Info_triggered();

	void on_actionTrain_filtered_triggered();

	void on_pushModify_clicked();

private:
	enum InvestigationType {
		INVEST_ABILITY,
		INVEST_OVERALL,
		INVEST_PASSING,
	};

	Ui::MainWindow *ui;
	QMap<QString, PlayerData *> nameIndex;
	QList<PlayerData *> players;
	QHash<QString, QHash<QString, QVariant> > playerdb;
	QList<QHash<QString, QVariant> > teamdb;
	QStringList comparisons;
	QList<PlayerData *> modifications;
	PlayerData *currentPlayer;

	void filterByTeam(const QString &arg1);
	void filterByFilters(const QStringList &filters);
	void filterByName(const QString &arg1);
	void filterBestAttackers();
	void filterBestMidfielders();
	void filterBestPlayers();
	QStringList sortItems(QStringList items);
	double investigateTeam(int tid, InvestigationType type);
};

#endif // MAINWINDOW_H
