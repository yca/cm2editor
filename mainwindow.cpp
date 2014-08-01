#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMap>
#include <QFile>
#include <QHash>
#include <QList>
#include <QDebug>

#include <math.h>
#include <errno.h>

//#define r16(_x) ((((uchar)((_x)[0])) << 16) + ((uchar)((_x)[1])))
#define r16(_x) (*(short *)(_x))
#define r32(_x) (*(int *)(_x))
#define r32le(_x) ((uchar)((_x)[0] << 24) | ((uchar)(_x)[1] << 16) | ((uchar)(_x)[2] << 8) | ((uchar)(_x)[3] << 0))

QHash<QString, PlayerAtr> PlayerData::attrIndex;

int ratingAdjustment = 0;
static inline double cfact(PlayerData *pd)
{
	double cf = 1;
	if (ratingAdjustment == 1)
		cf = pd->ability / 200.0;
	else if (ratingAdjustment == 2)
		cf = sqrt(pd->ability / 200.0);
	else if (ratingAdjustment == 3)
		cf = pd->ability / 200.0 * pd->potential / 200.0;
	return cf;
}

static double overallRating(PlayerData *pd)
{
	int sum = 0;
	for (int i = 0; i < ATT_COUNT; i++) {
		if (i == ATT_UNKNOWN_ADAP || i == ATT_UNKNOWN1 || i == ATT_UNKNOWN6)
			continue;
		if (i == ATT_INJURY || i == ATT_DIRTY || i == ATT_AGGRESION)
			sum += (20 - pd->atts[i]);
		else
			sum += pd->atts[i];
	}
	double avg = (double)sum / (ATT_COUNT - 3);
	return avg * cfact(pd);
}

static double attackRating(PlayerData *pd)
{
	int sum = pd->atts[ATT_PASSING] + pd->atts[ATT_HEADING] + pd->atts[ATT_STAMINA] + pd->atts[ATT_STREN] + pd->atts[ATT_CREAT] + pd->atts[ATT_SHOOTING];
	return sum / 6.0 * cfact(pd);
}

static double midfieldRating(PlayerData *pd)
{
	int sum = pd->atts[ATT_PASSING] + pd->atts[ATT_HEADING] + pd->atts[ATT_STAMINA] + pd->atts[ATT_STREN]
			+ pd->atts[ATT_TACK] + pd->atts[ATT_POSI];
	return sum / 6.0 * cfact(pd);
}

static double wingerRating(PlayerData *pd)
{
	int sum = pd->atts[ATT_PASSING] + pd->atts[ATT_HEADING] + pd->atts[ATT_STAMINA] + pd->atts[ATT_STREN]
			+ pd->atts[ATT_TACK] + pd->atts[ATT_POSI] + pd->atts[ATT_PACE] + pd->atts[ATT_CREAT]
			+ pd->atts[ATT_OFFTHEBALL];
	return sum / 9.0 * cfact(pd);
}

static QMap<QString, PlayerData *> *sortIndex;
static bool abilityLessThan(const QString &s1, const QString &s2)
{
	PlayerData *pd1 = sortIndex->value(s1);
	PlayerData *pd2 = sortIndex->value(s2);
	return pd1->ability < pd2->ability;
}
static bool attackerLessThan(const QString &s1, const QString &s2)
{
	PlayerData *pd1 = sortIndex->value(s1);
	PlayerData *pd2 = sortIndex->value(s2);
	return attackRating(pd1) < attackRating(pd2);
}
static bool midfielderLessThan(const QString &s1, const QString &s2)
{
	PlayerData *pd1 = sortIndex->value(s1);
	PlayerData *pd2 = sortIndex->value(s2);
	return midfieldRating(pd1) < midfieldRating(pd2);
}
static bool wingerLessThan(const QString &s1, const QString &s2)
{
	PlayerData *pd1 = sortIndex->value(s1);
	PlayerData *pd2 = sortIndex->value(s2);
	return wingerRating(pd1) < wingerRating(pd2);
}
static bool overallLessThan(const QString &s1, const QString &s2)
{
	PlayerData *pd1 = sortIndex->value(s1);
	PlayerData *pd2 = sortIndex->value(s2);
	return overallRating(pd1) < overallRating(pd2);
}

class Parser
{
public:
	virtual int parse(const QString &filename) = 0;
};

class Modifier
{
public:
	QList<PlayerData *> modifications;
	virtual int parseAndModify(const QString &filename) = 0;
};

class PldataModifier : public Modifier
{
public:
	virtual int parseAndModify(const QString &filename)
	{
		QFile f(filename);
		if (!f.open(QIODevice::ReadOnly))
			return -ENOENT;
		QByteArray ba = f.readAll();
		f.close();

		char *data = ba.data();
		/* modify data */
		foreach (PlayerData *pl, modifications) {
			if (pl->potential > 200)
				pl->potential = 200;
			if (pl->ability > pl->potential)
				pl->ability = pl->potential;
			data[pl->plpos + 26] = pl->ability;
			data[pl->plpos + 28] = pl->potential;
			data[pl->plpos + 30] = pl->reputation;
			char *atts = data + pl->plpos + 233;
			for (int i = 0; i < ATT_COUNT; i++) {
				atts[i] = pl->atts[i];
				if (atts[i] > 20)
					atts[i] = 20;
			}
		}

		if (!f.open(QIODevice::WriteOnly))
			return -ENOENT;
		f.write(ba);
		f.close();

		return 0;
	}
};

class TmdataParser : public Parser
{
public:
	virtual int parse(const QString &filename)
	{
		QFile f(filename);
		if (!f.open(QIODevice::ReadOnly))
			return -ENOENT;
		QByteArray ba = f.readAll();
		f.close();

		if (filename.endsWith(".DB1"))
			return parseDb(ba);

		return -EINVAL;
	}
	QHash<QString, QHash<QString, QVariant> > teamdb;
	QList<QHash<QString, QVariant> > teamdb2;
protected:
	virtual int parseDb(const QByteArray &ba)
	{
		const char *data = ba.constData();
		int size = ba.size();
		int pos = 0;
		int total = 0;

		QList<QPair<QString, int> > fields;
		while (1) {
			int flen = data[pos + 2];
			QString field = QString::fromLatin1(data + pos + 3, flen);
			int size = data[pos + 3 + flen + 1];
			pos = pos + 3 + flen + 2;
			total += size;
			fields << QPair<QString, int>(field, size);
			if (pos > 886)
				break;
		}
		//qDebug() << fields << total << size;
		pos = 0x37f;
		while (pos < size) {
			QHash<QString, QVariant> values;
			int off = 0;
			for (int i = 0; i < fields.size(); i++) {
				const char *p = data + pos + off;
				QPair<QString, int> field = fields[i];
				if (field.second > 4)
					values.insert(field.first, QString::fromUtf8(p));
				else if (field.second == 1)
					values.insert(field.first, p[0]);
				else if (field.second == 2)
					values.insert(field.first, r16(p));
				else if (field.second == 4)
					values.insert(field.first, r32le(p));
				off += field.second;
			}
			pos += total;
			teamdb.insert(values["UK Long Name"].toString(), values);
			teamdb2 << values;
		}
		return 0;
	}
};

class PldataParser : public Parser
{
public:
	enum ParseState {
		UNDEFINED,
		MARKER_0000,
		MARKER_0001,
		NAME_READ,
	};

	virtual int parse(const QString &filename)
	{
		QFile f(filename);
		if (!f.open(QIODevice::ReadOnly))
			return -ENOENT;
		QByteArray ba = f.readAll();
		f.close();

		//return countSignatures(ba.constData(), ba.size());

		const char *data = ba.constData();
		if (filename.endsWith(".DB1"))
			return parseV3(data, ba.size());
		return parseV2(data, ba.size());
	}
	QList<PlayerData *> players;
	QHash<QString, QHash<QString, QVariant> > playerdb;
private:

	PlayerData * parsePayloadV1(const char *data, int size)
	{
		struct PlayerData *pdata = new PlayerData;
		pdata->bday = data[20];
		pdata->bmonth = data[21];
		pdata->byear = data[22];
		pdata->age = 97 - data[22];
		pdata->teamId = r16(data + 15);
		pdata->ability = (uchar)data[26];
		pdata->potential = (uchar)data[28];
		pdata->reputation = (uchar)data[30];
		pdata->cyear = (uchar)data[108];
		pdata->trAvail = (PlayerTransferStatus)data[138];
		pdata->trValue = (PlayerTransferValue)data[139];

		const char *adata = data + 233;
		for (int i = 0; i < ATT_COUNT; i++)
			pdata->atts[i] = adata[i];

		memcpy(pdata->payload, data, size);
		return pdata;
	}

	int parseV3(const char *data, int size)
	{
		int pos = 0;
		int total = 0;

		QList<QPair<QString, int> > fields;
		while (1) {
			int flen = data[pos + 2];
			QString field = QString::fromLatin1(data + pos + 3, flen);
			//int type = data[pos + 3 + flen];
			int size = data[pos + 3 + flen + 1];
			pos = pos + 3 + flen + 2;
			total += size;
			fields << QPair<QString, int>(field, size);
			//qDebug() << field << type << size << pos;
			if (pos > 657)
				break;
		}
		//qDebug() << fields << total << size;
		pos = 0x29A;
		while (pos < size) {
			QHash<QString, QVariant> values;
			int off = 0;
			for (int i = 0; i < fields.size(); i++) {
				const char *p = data + pos + off;
				QPair<QString, int> field = fields[i];
				if (field.second > 4)
					values.insert(field.first, QString::fromUtf8(p));
				else if (field.second == 1)
					values.insert(field.first, p[0]);
				else if (field.second == 2)
					values.insert(field.first, r16(p));
				else if (field.second == 4)
					values.insert(field.first, r32(p));
				off += field.second;
			}
			pos += total;
			playerdb.insert(QString("%1 %2").arg(values["First Name"].toString())
					.arg(values["Second Name"].toString()), values);
			//qDebug() << (uchar)data[pos] << (uchar)data[pos + 1] << (uchar)data[pos + 2] << (uchar)data[pos + 3];
		}
		return 0;
	}

	int parseV2(const char *data, int size)
	{
		int pos = -1;
		while (++pos < size) {
			/* first 2 bytes are not known yet */
			int id = players.size();
			int nlen = data[pos + 2];
			QString name = QString::fromLatin1(data + pos + 4, nlen);
			int slen = data[pos + 4 + nlen];
			QString sname = QString::fromLatin1(data + pos + 4 + nlen + 2, slen);
			const char *payload = data + pos + 6 + nlen + slen;
			PlayerData *pdata = parsePayloadV1(payload, 358);
			pdata->name = name;
			pdata->sname = sname;
			pdata->id = id;
			pdata->fpos = pos;
			pdata->plpos = pos + 6 + nlen + slen;
			players << pdata;
			pos += 2 + 2 + 2 + nlen + slen + 358 - 1;
		}

		return 0;
	}

	int parseV1(const char *data, int size)
	{
		int pos = -1;

		int total = 6;

		QList<int> sMarkers;
		sMarkers << 0x0005;
		sMarkers << 0x0006;
		sMarkers << 0x0007;

		ParseState state = UNDEFINED;
		while (++pos < size) {
#if 0
			//qDebug() << "pos" << pos << "of" << ba.size() << r16(data + pos);

			if (!sMarkers.contains(r16(data + pos)))
				continue;

			pos += 2;
			if (data[pos] < 0x41)
				continue;
			QString name = QString::fromAscii(data + pos);
			qDebug() << "found name" <<  name << "at pos" << pos;
			pos += name.size();
#else
			//int v16 = r16(data + pos);
			int v32 = r32(data + pos);
			switch (state) {
			case UNDEFINED:
				if (!v32) {
					state = MARKER_0000;
					pos += 3;
					//qDebug() << "marker at" << pos - 3;
				} /*else if (v32 == 0x1) {
					state = MARKER_0001;
					pos += 3;
				}*/
				break;
			case MARKER_0001:
			case MARKER_0000:
				if (v32) {
					//qDebug() << "to name read at" << pos;
					if (1 || v32 & 0x80)
						state = NAME_READ;
					else
						state = UNDEFINED;
					pos += 3;
				} else
					pos += 3;
				break;
			case NAME_READ:
				/* we should have at least a 2 char name huh? that's why we have "+ 1" */
				if (data[pos + 0] < 0x41 || data[pos + 1] < 0x41) {
					//qDebug() << "to undefined at" << pos + 4;
					state = UNDEFINED;
					break;
				}
				QString name = QString::fromLatin1(data + pos);
				qDebug() << "found name" <<  name << "at pos" << pos;
				if (--total <= 0)
					return -1;
				pos += name.size() - 1;
				state = UNDEFINED;
				break;
			}
#endif
		}

		return 0;
	}

	int countSignatures(const char *data, int size)
	{
		QHash<int, int> s32;
		for (int i = 0; i < size; i += 4) {
			if (i + 4 > size)
				break;
			s32[r32(data + i)]++;
		}
		QHashIterator<int, int> i(s32);
		while(i.hasNext()) {
			i.next();
			if (i.value() > 10000)
				qDebug() << i.key() << i.value();
		}

		return 0;
	}
};

static QString attName(PlayerAtr att)
{
	if (att == ATT_UNKNOWN_ADAP)
		return "Adaptibility";
	if (att == ATT_AGGRESION)
		return "Aggression";
	if (att == ATT_BIGOCC)
		return "Big occasions";
	if (att == ATT_CHARACTER)
		return "Character";
	if (att == ATT_CONSIS)
		return "Consistency";
	if (att == ATT_CREAT)
		return "Creativity";
	if (att == ATT_DETER)
		return "Determination";
	if (att == ATT_DIRTY)
		return "Dirtyness";
	if (att == ATT_DRIB)
		return "Dribbling";
	if (att == ATT_FLAIR)
		return "Flair";
	if (att == ATT_HEADING)
		return "Heading";
	if (att == ATT_INFLU)
		return "Influence";
	if (att == ATT_INJURY)
		return "Injury proneness";
	if (att == ATT_OFFTHEBALL)
		return "Off the ball";
	if (att == ATT_PACE)
		return "Pace";
	if (att == ATT_PASSING)
		return "Passing";
	if (att == ATT_POSI)
		return "Positioning";
	if (att == ATT_SETP)
		return "Set pieces";
	if (att == ATT_SHOOTING)
		return "Shooting";
	if (att == ATT_STAMINA)
		return "Stamina";
	if (att == ATT_STREN)
		return "Strength";
	if (att == ATT_TACK)
		return "Tackling";
	if (att == ATT_TECH)
		return "Technique";
	return "Unknown";
}

#define SAVE_FILE "/home/caglar/myfs/temp/cm9798/PLDATA3.S16"
#define PLAYERSDB1 "/home/caglar/myfs/temp/cm9798/PLAYERS.DB1"
#define TEAMDB1 "/home/caglar/myfs/temp/cm9798/TMDATA.DB1"

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	TmdataParser p3;
	p3.parse(TEAMDB1);
	qDebug() << p3.teamdb.size() << "teams found";
	teamdb = p3.teamdb2;

	PldataParser p;
	p.parse(SAVE_FILE);
	qDebug() << p.players.size() << "players found";

	PldataParser p2;
	p2.parse(PLAYERSDB1);
	playerdb = p2.playerdb;

	players = p.players;
	for (int i = 0; i < players.size(); i++) {
		nameIndex.insert(QString("%1, %2").arg(players[i]->name)
						 .arg(players[i]->sname), players[i]);

		/* fill players positional information */
		PlayerData *pl = players[i];
		QString key = QString("%1 %2").arg(players[i]->name).arg(players[i]->sname);
		if (playerdb.contains(key)) {
			QHash<QString, QVariant> values = playerdb[key];

			pl->positions[PlayerData::GK] = values["Goalkeeper"].toInt();
			pl->positions[PlayerData::SW] = values["Sweeper"].toInt();
			pl->positions[PlayerData::DEF] = values["Defence"].toInt();
			pl->positions[PlayerData::ANC] = values["Anchor"].toInt();
			pl->positions[PlayerData::MID] = values["Midfield"].toInt();
			pl->positions[PlayerData::SUP] = values["Support"].toInt();
			pl->positions[PlayerData::ATT] = values["Attack"].toInt();
			pl->sides[PlayerData::LEFT] = values["Left Sided"].toInt();
			pl->sides[PlayerData::RIGHT] = values["Right Sided"].toInt();
			pl->sides[PlayerData::CENTRAL] = values["Central"].toInt();
		}
	}
	ui->listPlayers->addItems(nameIndex.keys());

	ui->comboAdjustment->setCurrentIndex(2);

	for (int i = 0; i < ATT_COUNT; i++)
		PlayerData::attrIndex.insert(attName((PlayerAtr)i), (PlayerAtr)i);

	currentPlayer = NULL;
}

MainWindow::~MainWindow()
{
	delete ui;
}

void MainWindow::on_listPlayers_currentRowChanged(int currentRow)
{
	if (currentRow < 0)
		return;
	QString nind = ui->listPlayers->item(currentRow)->text();
	PlayerData *data = nameIndex[nind];
	currentPlayer = data;
	QStringList lines;
	lines << QString("name: %1 %2").arg(data->name).arg(data->sname);
	lines << QString("birth: %1.%2.%3").arg(data->bday).arg(data->bmonth).arg(data->byear);
	lines << QString("team: %1 (%2)").arg(data->teamId).arg(teamdb[data->teamId]["UK Long Name"].toString());
	lines << QString("ability: %1").arg(data->ability);
	lines << QString("potential: %1").arg(data->potential);
	lines << QString("reputation: %1").arg(data->reputation);
	lines << QString("overall: %1").arg(overallRating(data));
	lines << QString("transfer status: %1").arg(data->trAvail);
	lines << QString("transfer value: %1").arg(data->trValue);
	lines << QString("file offset: %1").arg(data->fpos);

	QString key = QString("%1 %2").arg(data->name).arg(data->sname);
	if (playerdb.contains(key)) {
		QHash<QString, QVariant> values = playerdb[key];
		lines << QString("Nation: %1").arg(values["Nation"].toString());
		lines << QString("Goalkeeper: %1").arg(values["Goalkeeper"].toString());
		lines << QString("Sweeper: %1").arg(values["Sweeper"].toString());
		lines << QString("Defence: %1").arg(values["Defence"].toString());
		lines << QString("Anchor: %1").arg(values["Anchor"].toString());
		lines << QString("Midfield: %1").arg(values["Midfield"].toString());
		lines << QString("Support: %1").arg(values["Support"].toString());
		lines << QString("Attack: %1").arg(values["Attack"].toString());
		lines << QString("Right Sided: %1").arg(values["Right Sided"].toString());
		lines << QString("Left Sided: %1").arg(values["Left Sided"].toString());
		lines << QString("Central: %1").arg(values["Central"].toString());
	}
	/* show attributes, but sorted ;) */
	lines << "========== Attributes ==========";
	QMap<QString, int> atts;
	for (int i = 0; i < ATT_COUNT; i++)
		atts.insert(QString("%1: %2").arg(attName((PlayerAtr)i)).arg(data->atts[i]), 0);
	lines << atts.keys();

	/* show raw data */
	lines << "========== Payload ==========";
	QString line = "0: ";
	for (int i = 0; i < 358; i++) {
		line.append(QString("%1 ").arg((uchar)data->payload[i], 2, 16, QChar('0')));
		if (i % 8 == 7) {
			lines << line;
			line = QString("%1: ").arg(i / 8 * 8 + 8);
		}
	}
	lines << line;

	ui->plainPlayerInfo->setPlainText(lines.join("\n"));

}

void MainWindow::on_lineSearchPlayer_textChanged(const QString &arg1)
{
	ratingAdjustment = ui->comboAdjustment->currentIndex();

	if (arg1.startsWith(":")) {
		filterByTeam(arg1);
	} else if (arg1.startsWith("@")) {
		QStringList filters = arg1.split(",");
		filterByFilters(filters);
	} else if (arg1.startsWith("!")) {
		if (arg1.contains("attackers"))
			filterBestAttackers();
		if (arg1.contains("midfielders"))
			filterBestMidfielders();
	} else
		filterByName(arg1);
}

void MainWindow::on_listPlayers_itemDoubleClicked(QListWidgetItem *item)
{
	if (comparisons.contains(item->text()))
		return;
	comparisons << item->text();
	if (comparisons.size() < 2)
		return;
	PlayerData *ref = nameIndex[comparisons[0]];
	qDebug() << comparisons;
	for (int i = 0; i < 358; i++) {
		char refb = ref->payload[i];
		bool same = true;
		for (int j = 1;  j < comparisons.size(); j++) {
			PlayerData *p = nameIndex[comparisons[j]];
			if (refb != p->payload[i]) {
				same = false;
				break;
			}
		}
		if (same && refb != 0)
			qDebug() << i << (uchar)refb;
	}
}

void MainWindow::filterByTeam(const QString &arg1)
{
	int tid = arg1.split(":")[1].toInt();

	QStringList items;
	QMapIterator<QString, PlayerData *> i(nameIndex);
	while (i.hasNext()) {
		i.next();
		PlayerData *pdata = i.value();
		if (pdata->teamId != tid)
			continue;
		items << i.key();
	}
	ui->listPlayers->clear();
	items = sortItems(items);
	ui->listPlayers->addItems(items);
}

void MainWindow::filterByFilters(const QStringList &filters)
{
	QStringList items;
	QMapIterator<QString, PlayerData *> i(nameIndex);
	while (i.hasNext()) {
		i.next();
		PlayerData *pdata = i.value();
		bool add = true;
		foreach (QString filter, filters) {
			filter.remove("@");
			QStringList args = filter.split(" ");
			if (args.size() < 3) {
				add = false;
				break;
			}
			int val = 0;
			if (args[0] == "a")
				val = pdata->ability;
			else if (args[0] == "p")
				val = pdata->potential;
			else if (args[0] == "r")
				val = pdata->reputation;
			else if (args[0] == "o")
				val = overallRating(pdata);
			else if (args[0] == "tid")
				val = pdata->teamId;
			else if (args[0] == "trst")
				val = pdata->trAvail;
			else if (args[0] == "trval")
				val = pdata->trValue;
			else if (args[0] == "cyear")
				val = pdata->cyear;
			else if (args[0] == "cyear")
				val = pdata->cyear;
			else if (args[0] == "trep")
				val = teamdb[pdata->teamId]["Reputation"].toInt();
			else if (args[0] == "age")
				val = pdata->age;
			else if (args[0] == "goalkeeper")
				val = pdata->positions[PlayerData::GK];
			else if (args[0] == "sweeper")
				val = pdata->positions[PlayerData::SW];
			else if (args[0] == "defence")
				val = pdata->positions[PlayerData::DEF];
			else if (args[0] == "anchor")
				val = pdata->positions[PlayerData::ANC];
			else if (args[0] == "midfield")
				val = pdata->positions[PlayerData::MID];
			else if (args[0] == "support")
				val = pdata->positions[PlayerData::SUP];
			else if (args[0] == "attack")
				val = pdata->positions[PlayerData::ATT];
			else if (args[0] == "left")
				val = pdata->sides[PlayerData::LEFT];
			else if (args[0] == "right")
				val = pdata->sides[PlayerData::RIGHT];
			else if (args[0] == "central")
				val = pdata->sides[PlayerData::CENTRAL];
			else if (pdata->isAttribute(args[0]))
				val = pdata->attribute(args[0]);
			if (args[1] == "==" && args[2].toInt() != val)
				add = false;
			if (args[1] == ">" && args[2].toInt() > val)
				add = false;
			if (args[1] == "<" && args[2].toInt() < val)
				add = false;
			if (!add)
				break;
		}
		if (add)
			items << i.key();
	}
	ui->listPlayers->clear();
	items = sortItems(items);
	ui->listPlayers->addItems(items);
}

void MainWindow::filterByName(const QString &arg1)
{
	if (arg1.isEmpty()) {
		ui->listPlayers->clear();
		ui->listPlayers->addItems(nameIndex.keys());
		return;
	}

	QStringList items;
	QMapIterator<QString, PlayerData *> i(nameIndex);
	while (i.hasNext()) {
		i.next();
		if (!i.key().contains(arg1, Qt::CaseInsensitive))
			continue;
		items << i.key();
	}
	ui->listPlayers->clear();
	items = sortItems(items);
	ui->listPlayers->addItems(items);
}

void MainWindow::filterBestAttackers()
{
	QStringList items;
	QMapIterator<QString, PlayerData *> i(nameIndex);
	while (i.hasNext()) {
		i.next();
		PlayerData *pdata = i.value();
		double cf = cfact(pdata);
		if (pdata->atts[ATT_HEADING] * cf < 15.0)
			continue;
		if (pdata->atts[ATT_PASSING] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_STAMINA] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_STREN] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_CREAT] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_SHOOTING] * cf < 15.0)
			continue;
		items << i.key();
	}
	ui->listPlayers->clear();
	items = sortItems(items);
	ui->listPlayers->addItems(items);
}

void MainWindow::filterBestMidfielders()
{
	QStringList items;
	QMapIterator<QString, PlayerData *> i(nameIndex);
	while (i.hasNext()) {
		i.next();
		PlayerData *pdata = i.value();
		double cf = cfact(pdata);
		if (pdata->atts[ATT_HEADING] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_PASSING] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_STAMINA] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_STREN] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_TACK] * cf < 10.0)
			continue;
		if (pdata->atts[ATT_POSI] * cf < 10.0)
			continue;
		items << i.key();
	}
	ui->listPlayers->clear();
	items = sortItems(items);
	ui->listPlayers->addItems(items);
}

QStringList MainWindow::sortItems(QStringList items)
{
	sortIndex = &nameIndex;
	if (ui->comboSorting->currentIndex() == 0)
		return items;
	if (ui->comboSorting->currentIndex() == 1) {
		/* sort by ability */
		qSort(items.begin(), items.end(), abilityLessThan);
	}
	if (ui->comboSorting->currentIndex() == 2) {
		/* sort by attack rating */
		qSort(items.begin(), items.end(), attackerLessThan);
	}
	if (ui->comboSorting->currentIndex() == 3) {
		/* sort by overall rating */
		qSort(items.begin(), items.end(), overallLessThan);
	}
	if (ui->comboSorting->currentIndex() == 4) {
		/* sort by midfield rating */
		qSort(items.begin(), items.end(), midfielderLessThan);
	}
	if (ui->comboSorting->currentIndex() == 4) {
		/* sort by winger rating */
		qSort(items.begin(), items.end(), wingerLessThan);
	}
	return items;
}

double MainWindow::investigateTeam(int tid, enum InvestigationType type)
{
	QList<PlayerData *> players;
	QMapIterator<QString, PlayerData *> i(nameIndex);
	while (i.hasNext()) {
		i.next();
		PlayerData *pdata = i.value();
		if (pdata->teamId != tid)
			continue;
		players << pdata;
	}
	QMap<double, PlayerData *> ratings;
	for (int i = 0; i < players.size(); i++) {
		PlayerData *pdata = players[i];

		if (type == INVEST_ABILITY)
			ratings.insertMulti(pdata->ability, pdata);
		else if (type == INVEST_OVERALL)
			ratings.insertMulti(overallRating(pdata), pdata);
		else if (type == INVEST_PASSING)
			ratings.insertMulti(pdata->atts[ATT_PASSING], pdata);
	}

	QMapIterator<double, PlayerData *> j(ratings);
	double sum = 0;
	int tot = 14;
	j.toBack();
	while (j.hasPrevious()) {
		j.previous();
		sum += j.key();
		if (--tot == 0)
			break;
	}
	return sum / 14;
}

static QStringList ratingsToLines(const QMap<double, QString> &ratings)
{
	QStringList lines;
	QMapIterator<double, QString> j(ratings);
	j.toBack();
	while (j.hasPrevious()) {
		j.previous();
		lines << QString("%1: %2").arg(j.value()).arg(j.key());
	}
	return lines;
}

void MainWindow::on_actionLeague_Info_triggered()
{
	QMap<int, QString> teams;
#if 0
	teams.insert(681, "norwich");
	teams.insert(684, "oldham");
	teams.insert(691, "qpr");
	teams.insert(669, "man city");
	teams.insert(632, "crystal palace");
	teams.insert(599, "birmingham");
	teams.insert(722, "watford");
	teams.insert(654, "huddersfield");
	teams.insert(657, "ipswich");
	teams.insert(698, "sheff utd");
	teams.insert(690, "preston");
	teams.insert(606, "bradford");
	teams.insert(613, "bury");
	teams.insert(593, "aston villa");
	teams.insert(692, "reading");
	teams.insert(699, "sheff wed");
	teams.insert(688, "port vale");
	teams.insert(724, "w.b.a");
	teams.insert(689, "portsmouth");
	teams.insert(711, "stoke");
	teams.insert(710, "stockport");
	teams.insert(618, "charlton");
	teams.insert(712, "sunderland");
	teams.insert(685, "oxford");
#elif 0
	teams.insert(670, "man utd");
	teams.insert(666, "liverpool");
	teams.insert(675, "middlesbourgh");
	teams.insert(592, "arsenal");
	teams.insert(678, "newcastle united");
	teams.insert(602, "blackburn");
	teams.insert(620, "chelsea");
	teams.insert(629, "coventry");
	teams.insert(635, "derby");
	teams.insert(640, "everton");
	teams.insert(661, "leeds");
	teams.insert(663, "leicester");
	teams.insert(691, "qpr");
	teams.insert(669, "man city");
	teams.insert(593, "aston villa");
	teams.insert(682, "nottingham forest");
	teams.insert(703, "southampton");
	teams.insert(718, "tottenham");
	teams.insert(728, "wimbledon");
	teams.insert(730, "wolwes");
#elif 0
	QStringList list;
	list << "Juventus";
	list << "Udinese";
	list << "AC Milan";
	list << "Sampdoria";
	list << "Inter";
	list << "Vicenza";
	list << "Lazio";
	list << "Parma";
	list << "Bologna";
	list << "Lecce";
	list << "Bari";
	list << "Roma";
	list << "Fiorentina";
	list << "Piacenza";
	list << "Brescia";
	list << "Napoli";
	list << "Empoli";
	list << "Atalanta";
	for (int i = 0; i < teamdb.size(); i++) {
		QString lname = teamdb[i]["UK Long Name"].toString();
		QString sname = teamdb[i]["UK Short Name"].toString();
		if (list.contains(sname))
			teams.insert(i, sname);
		if (list.contains(lname))
			teams.insert(i, lname);
		if (list.size() == teams.size())
			break;
	}
#else
	QStringList list;
#if 0
	list << "Ancona";
	list << "Cagliari";
	list << "Castel di Sangro";
	list << "Chievo";
	list << "Fidelis Andria";
	list << "Foggia";
	list << "Genoa";
	list << "Lucchese";
	list << "Monza";
	list << "Padova";
	list << "Perugia";
	list << "Pescara";
	list << "Ravenna";
	list << "Reggiana";
	list << "Reggina";
	list << "Salernitana";
	list << "Torino";
	list << "Treviso";
	list << "Venezia";
	list << "Verona";
#else
	list << "Atalanta";
	list << "Bologna";
	list << "Cagliari";
	list << "Chievo";
	list << "Fiorentina";
	list << "Genoa";
	list << "Inter";
	list << "Juventus";
	list << "Lazio";
	list << "Milan";
	list << "Napoli";
	list << "Parma";
	list << "Ravenna";
	list << "Roma";
	list << "Salernitana";
	list << "Sampdoria";
	list << "Torino";
	list << "Udinese";
#endif
	for (int i = 0; i < teamdb.size(); i++) {
		QString lname = teamdb[i]["UK Long Name"].toString();
		QString sname = teamdb[i]["UK Short Name"].toString();
		if (list.contains(sname))
			teams.insert(i, sname);
		if (list.contains(lname))
			teams.insert(i, lname);
		if (list.size() == teams.size())
			break;
	}
	qDebug() << teams.size();
#endif
	QStringList lines;
	InvestigationType type = INVEST_ABILITY;
	QMap<double, QString> ratings;
	QMapIterator<int, QString> i(teams);
	while (i.hasNext()) {
		i.next();
		ratings.insertMulti(investigateTeam(i.key(), type), i.value());
	}
	//qDebug() << "ability rating: " << type << ratings;
	lines << QString("========= Ability =========");
	lines << ratingsToLines(ratings);

	type = INVEST_OVERALL;
	ratings.clear();
	i = QMapIterator<int, QString>(teams);
	while (i.hasNext()) {
		i.next();
		ratings.insertMulti(investigateTeam(i.key(), type), i.value());
	}
	//qDebug() << "overall rating: " << type << ratings;
	lines << QString("========= Overall =========");
	lines << ratingsToLines(ratings);

	type = INVEST_PASSING;
	ratings.clear();
	i = QMapIterator<int, QString>(teams);
	while (i.hasNext()) {
		i.next();
		ratings.insertMulti(investigateTeam(i.key(), type), i.value());
	}
	//qDebug() << "passing rating: "<< type << ratings;
	lines << QString("========= Passing =========");
	lines << ratingsToLines(ratings);

	QPlainTextEdit edit;
	edit.setPlainText(lines.join("\n"));
	edit.show();
	while (edit.isVisible())
		QApplication::processEvents();
}

void MainWindow::on_actionTrain_filtered_triggered()
{
	PldataModifier m;
	m.modifications = modifications;
	qDebug() << m.parseAndModify(SAVE_FILE);
}

void MainWindow::on_pushModify_clicked()
{
	QStringList lines = ui->plainPlayerInfo->toPlainText().split("\n");
	bool ok = false;
	foreach (QString line, lines) {
		if (line.contains("ability:"))
			currentPlayer->ability = line.split(":")[1].toInt();
		else if (line.contains("potential:"))
			currentPlayer->potential = line.split(":")[1].toInt();
		else if (line.contains("reputation:"))
			currentPlayer->reputation = line.split(":")[1].toInt();
		else if (line.contains("========== Attributes =========="))
			ok = true;
		else if (line.contains("========== Payload =========="))
			break;
		else if (!ok)
			continue;
		if (!line.contains(":"))
			continue;
		QStringList vals = line.split(":");
		qDebug() << vals[0] << vals[1].toInt() << currentPlayer->setAttribute(vals[0], vals[1].toInt());
	}
	if (!modifications.contains(currentPlayer))
		modifications << currentPlayer;
	ui->actionTrain_filtered->setEnabled(true);
}
