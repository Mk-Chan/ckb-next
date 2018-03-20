#include <QMenu>
#include "kbprofiledialog.h"
#include "ui_kbprofiledialog.h"
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
#include "quazip/JlCompress.h"
#include <QCryptographicHash>

KbProfileDialog::KbProfileDialog(KbWidget *parent) :
    QDialog(parent),
    ui(new Ui::KbProfileDialog), device(parent->device)
{
    ui->setupUi(this);
    connect(ui->profileList, SIGNAL(orderChanged()), this, SLOT(profileList_reordered()));

    // Populate profile list
    repopulate();
    //activeProfile = device->newProfile(device->currentProfile());
}

KbProfileDialog::~KbProfileDialog(){
    delete ui;
}

void KbProfileDialog::profileList_reordered(){
    // Rebuild profile list from items
    QList<KbProfile*> newProfiles;
    int count = ui->profileList->count();
    for(int i = 0; i < count; i++){
        QListWidgetItem* item = ui->profileList->item(i);
        KbProfile* profile = device->find(item->data(GUID).toUuid());
        if(profile && !newProfiles.contains(profile))
            newProfiles.append(profile);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    // Add any missing profiles at the end of the list
    foreach(KbProfile* profile, device->profiles()){
        if(!newProfiles.contains(profile))
            newProfiles.append(profile);
    }
    device->profiles(newProfiles);
}

void KbProfileDialog::repopulate(){
    ui->profileList->clear();
    QListWidgetItem* current = 0;
    foreach(KbProfile* profile, device->profiles()){
        QListWidgetItem* item = new QListWidgetItem(QIcon((profile == device->hwProfile()) ? ":/img/icon_profile_hardware.png" : ":/img/icon_profile.png"), profile->name(), ui->profileList);
        item->setData(GUID, profile->id().guid);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if(profile == device->currentProfile()){
            item->setSelected(true);
            current = item;
        }
        ui->profileList->addItem(item);
    }
    if(current)
        ui->profileList->setCurrentItem(current);
    addNewProfileItem();
}

void KbProfileDialog::addNewProfileItem(){
    // Add an item for creating a new profile. Make it editable but not dragable.
    QListWidgetItem* item = new QListWidgetItem("New profile...", ui->profileList);
    item->setFlags((item->flags() | Qt::ItemIsEditable) & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsDropEnabled);
    item->setData(NEW_FLAG, 1);
    QFont font = item->font();
    font.setItalic(true);
    item->setFont(font);
    item->setIcon(QIcon(":/img/icon_plus.png"));
    ui->profileList->addItem(item);
}

void KbProfileDialog::on_profileList_itemClicked(QListWidgetItem *item){
    QUuid guid = item->data(GUID).toUuid();
    if(guid.isNull() && item->data(NEW_FLAG).toInt() == 1){
        // New profile
        item->setText("");
        ui->profileList->editItem(item);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        QFont font = item->font();
        font.setItalic(false);
        item->setFont(font);
        item->setIcon(QIcon(":/img/icon_profile.png"));
        // Add the new profile and assign it to this item
        KbProfile* newProfile = device->newProfile();
        device->appendProfile(newProfile);
        item->setData(GUID, newProfile->id().guid);
        item->setData(NEW_FLAG, 0);
        device->setCurrentProfile(newProfile);
        // Create another "new profile" item to replace this one
        addNewProfileItem();
    }
}

void KbProfileDialog::on_profileList_itemChanged(QListWidgetItem *item){
    KbProfile* currentProfile = device->currentProfile();
    if(!item || !currentProfile || item->data(GUID).toUuid() != currentProfile->id().guid)
        return;
    currentProfile->name(item->text());
    // Set the text to the actual name (trimmed, "" replaced with "Unnamed")
    item->setText(currentProfile->name());
}

void KbProfileDialog::on_profileList_customContextMenuRequested(const QPoint &pos){
    QListWidgetItem* item = ui->profileList->itemAt(pos);
    KbProfile* currentProfile = device->currentProfile();
    if(!item || !currentProfile || item->data(GUID).toUuid() != currentProfile->id().guid)
        return;
    int index = device->indexOf(currentProfile);
    QList<KbProfile*> profiles = device->profiles();

    QMenu menu(this);
    QAction* rename = new QAction("Rename...", this);
    QAction* duplicate = new QAction("Duplicate", this);
    QAction* del = new QAction("Delete", this);
    bool canDelete = (profiles.count() > 1);
    if(!canDelete)
        // Can't delete the last profile on the device
        del->setEnabled(false);
    QAction* hwsave = new QAction("Save to Hardware", this);
    QAction* moveup = new QAction("Move Up", this);
    if(index == 0)
        moveup->setEnabled(false);
    QAction* movedown = new QAction("Move Down", this);
    if(index >= profiles.count() - 1)
        movedown->setEnabled(false);
    menu.addAction(rename);
    menu.addAction(duplicate);
    menu.addAction(del);
    menu.addSeparator();
    menu.addAction(hwsave);
    menu.addSeparator();
    menu.addAction(moveup);
    menu.addAction(movedown);
    QAction* result = menu.exec(QCursor::pos());
    if(result == rename){
        ui->profileList->editItem(item);
    } else if(result == duplicate){
        KbProfile* newProfile = device->newProfile(currentProfile);
        newProfile->newId();
        profiles.insert(index + 1, newProfile);
        device->profiles(profiles);
        device->setCurrentProfile(newProfile);
    } else if(result == del){
        if(!canDelete)
            return;
        profiles.removeAll(currentProfile);
        device->profiles(profiles);
        currentProfile->deleteLater();
        // Select next profile
        if(index < profiles.count())
            device->setCurrentProfile(profiles.at(index));
        else
            device->setCurrentProfile(profiles.last());
    } else if(result == hwsave){
        device->hwSave();
        // Refresh item icons
        int count = ui->profileList->count();
        for(int i = 0; i < count; i++){
            QListWidgetItem* item = ui->profileList->item(i);
            if(item->data(NEW_FLAG).toUInt() == 1)
                continue;
            KbProfile* profile = device->find(item->data(GUID).toUuid());
            item->setIcon(QIcon((profile == device->hwProfile()) ? ":/img/icon_profile_hardware.png" : ":/img/icon_profile.png"));
        }
    } else if(result == moveup){
        profiles.removeAll(currentProfile);
        profiles.insert(index - 1, currentProfile);
        device->profiles(profiles);
    } else if(result == movedown){
        profiles.removeAll(currentProfile);
        profiles.insert(index + 1, currentProfile);
        device->profiles(profiles);
    }
    repopulate();
}


void KbProfileDialog::on_exportButton_clicked()
{
    // Selected items
    QList<QListWidgetItem*> selectedItems = ui->profileList->selectedItems();
    QList<KbProfile*> profiles = device->profiles();

    // Set up the file dialog
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(tr("ckb-next profiles (*.ckb)"));
    dialog.setViewMode(QFileDialog::List);
    dialog.setAcceptMode(QFileDialog::AcceptSave);

    if(!dialog.exec())
        return;

    QStringList filenames = dialog.selectedFiles();

    if(filenames.empty())
        return;

    // Pick only the first filename
    QString filename = filenames.at(0);
    if(!filename.endsWith(".ckb"))
        filename.append(".ckb");

    QStringList tmpExported;
    // Create a QSettings ini in /tmp/ for each selected profile
    for(int p = 0; p < selectedItems.count(); p++)
    {
        // Get the profile's pointer
        QListWidgetItem* item = selectedItems.at(p);
        KbProfile* prof = device->find(item->data(GUID).toUuid());

        QString tmp("/tmp/ckbprofile");
        tmp.append(QString::number(p)); // TODO: tmp.append(profname.toLower());
        tmp.append(".ini");

        // Used to make sure QSettings is destroyed before trying to hash the ini
        {
            QSettings exportitem(tmp, QSettings::IniFormat);
            exportitem.clear();
            prof->profileExport(&exportitem);
            exportitem.sync();

            if(exportitem.status() == QSettings::NoError)
                tmpExported.append(tmp);
        }

        // Create a hash file for the generated ini
        QFile tmpHashFile(tmp);
        if(tmpHashFile.open(QFile::ReadOnly))
        {
            QCryptographicHash tmpHash(QCryptographicHash::Sha256);
            if(tmpHash.addData(&tmpHashFile))
            {
                tmp.append(".s256");
                QFile tmpHashFileW(tmp);
                if(tmpHashFileW.open(QFile::WriteOnly))
                {
                    tmpExported.append(tmp);
                    QTextStream hashStream(&tmpHashFileW);
                    hashStream << tmpHash.result().toHex() << "\n";
                }
            }
        }

    }

    if(!JlCompress::compressFiles(filename, tmpExported))
        QMessageBox::warning(this, tr("Error"), tr("An error occured when exporting the selected profiles."), QMessageBox::Ok);

    for(int i = 0; i < tmpExported.count(); i++)
    {
        QFile fdel(tmpExported.at(i));
        fdel.remove();
    }

    QMessageBox::information(this, tr("Export Successful"), tr("Selected profiles have been exported successfully."), QMessageBox::Ok);
}

void KbProfileDialog::on_importButton_clicked()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(tr("ckb-next profiles (*.ckb)"));

    if(!dialog.exec())
        return;

    //QMessageBox::warning(this, "Not Implemented", "Not Implemented");

    QStringList filenames = dialog.selectedFiles();

    // Pick only the first filename
    QString filename = filenames.at(0);
    if(!filename.endsWith(".ckb"))
        qDebug() << "Err";

    // Extract the ckb package
    QStringList extracted = JlCompress::extractDir(filename, QString("/tmp/"));
    // TODO Better error handling for below
    if(extracted.empty())
        QMessageBox::warning(this, tr("Error"), QString(tr("Could not extract %1.")).arg(filename), QMessageBox::Ok);

    QStringList profilestr;
    // <Pointer>, <GUID>
    QList<QPair<QSettings*, QString>> profileptrs;

    for(int i = 0; i < extracted.count(); i++)
    {
        QString tmpFile = extracted.at(i);
        if(tmpFile.endsWith("ini"))
        {
            QFile iniFile(tmpFile);
            tmpFile.append(".s256");
            QFile hashFile(tmpFile);
            if(iniFile.exists() && hashFile.exists())
            {
                if(iniFile.open(QFile::ReadOnly) && hashFile.open(QFile::ReadOnly))
                {
                    // Max sha256 string length is 64
                    QByteArray pkgHash = QByteArray::fromHex(hashFile.read(64).data());

                    QCryptographicHash iniHash(QCryptographicHash::Sha256);
                    if(iniHash.addData(&iniFile))
                    {
                        hashFile.close();
                        iniFile.close();
                        if(iniHash.result() == pkgHash)
                        {
                            QSettings* sptr = new QSettings(extracted.at(i), QSettings::IniFormat);
                            QString guid(sptr->childGroups().first());

                            profileptrs.append(QPair<QSettings*, QString>(sptr, guid));
                            profilestr.append(sptr->value(guid + "/Name").toString());
                        }
                    }
                }
            }
        }
    }

    // TODO use a proper widget with a profile picker
    // For now, import everything
    QString profilestrf(profilestr.join("\n• "));
    int ret = QMessageBox::information(this, tr("Profile Import"),
                                   QString(tr("The following profiles will be imported.\n\n• ")).append(profilestrf),
                                   QMessageBox::Ok,
                                   QMessageBox::Cancel);
    if(ret == QMessageBox::Ok)
    {
        QList<KbProfile*> profiles = device->profiles();

        for(int i = 0; i < profileptrs.count(); i++)
        {
            int ret = 0;
            QSettings* sptr = profileptrs.at(i).first;
            //QString guid = profileptrs.at(i).second;
            //QUuid current = guid.trimmed();
            QUuid guid = sptr->childGroups().first().trimmed();
            // Messy, shhhh
            QString profname = profilestr.at(i);
            KbProfile* profilematch = nullptr;
            foreach(KbProfile* profile, device->profiles()){

                if(profile->id().guid == guid)
                {
                    ret = QMessageBox::question(this, tr("Profile Import"),
                                                profname + tr(" already exists. Overwrite it?"),
                                                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                                QMessageBox::Cancel);
                    profilematch = profile;
                    break;
                }
            }

            if(ret == QMessageBox::Cancel)
                continue;

            if(ret == QMessageBox::Yes)
                profiles.removeAll(profilematch);

            if(ret == QMessageBox::No)
                qDebug() << "Guid change goes here";

            qDebug() << "Importing" << profname;

            // Import code goes here
        }
        device->profiles(profiles);
        qDebug() << profilestr;
    }

    // Clean up

    // Destroy the open QSettings objects
    for(int i = 0; i < profileptrs.count(); i++)
        delete profileptrs.at(i).first;

    for(int i = 0; i < extracted.count(); i++)
    {
        // Delete extracted files
        QFile fdel(extracted.at(i));
        fdel.remove();
    }

    repopulate();

}

void KbProfileDialog::on_buttonBox_accepted()
{
    // If the user clicked ok with multiple profiles selected, revert to the previous one
    //if(ui->profileList->selectedItems().count() > 1 && activeProfile != nullptr)
    //    device->setCurrentProfile(activeProfile);
}

void KbProfileDialog::on_profileList_itemSelectionChanged()
{
    // Only change the profile is a single item is selected
    if(ui->profileList->selectedItems().count() > 1)
        return;

    QList<QListWidgetItem*> itemlist = ui->profileList->selectedItems();
    if(itemlist.empty())
        return;

    KbProfile* profile = device->find(itemlist.first()->data(GUID).toUuid());
    if(!profile)
        return;

    device->setCurrentProfile(profile);
}
