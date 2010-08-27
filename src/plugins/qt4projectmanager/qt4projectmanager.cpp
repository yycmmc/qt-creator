/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "qt4projectmanager.h"

#include "qt4projectmanagerconstants.h"
#include "qt4projectmanagerplugin.h"
#include "qt4nodes.h"
#include "qt4project.h"
#include "qt4target.h"
#include "profilereader.h"
#include "qmakestep.h"
#include "qt4buildconfiguration.h"
#include "wizards/qmlstandaloneapp.h"

#include <coreplugin/icore.h>
#include <coreplugin/basefilewizard.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/uniqueidmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/session.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <utils/qtcassert.h>
#include <designer/formwindoweditor.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QVariant>
#include <QtGui/QFileDialog>
#include <QtGui/QMessageBox>

using namespace Qt4ProjectManager;
using namespace Qt4ProjectManager::Internal;

using ProjectExplorer::BuildStep;
using ProjectExplorer::FileType;
using ProjectExplorer::HeaderType;
using ProjectExplorer::SourceType;
using ProjectExplorer::FormType;
using ProjectExplorer::ResourceType;
using ProjectExplorer::UnknownFileType;

// Known file types of a Qt 4 project
static const char* qt4FileTypes[] = {
    "CppHeaderFiles",
    "CppSourceFiles",
    "Qt4FormFiles",
    "Qt4ResourceFiles"
};

Qt4Manager::Qt4Manager(Qt4ProjectManagerPlugin *plugin)
  : m_plugin(plugin),
    m_contextProject(0),
    m_lastEditor(0),
    m_dirty(false)
{
}

Qt4Manager::~Qt4Manager()
{
}

void Qt4Manager::registerProject(Qt4Project *project)
{
    m_projects.append(project);
}

void Qt4Manager::unregisterProject(Qt4Project *project)
{
    m_projects.removeOne(project);
}

void Qt4Manager::notifyChanged(const QString &name)
{
    foreach (Qt4Project *pro, m_projects)
        pro->notifyChanged(name);
}

void Qt4Manager::init()
{
    connect(Core::EditorManager::instance(), SIGNAL(editorAboutToClose(Core::IEditor*)),
            this, SLOT(editorAboutToClose(Core::IEditor*)));

    connect(Core::EditorManager::instance(), SIGNAL(currentEditorChanged(Core::IEditor*)),
            this, SLOT(editorChanged(Core::IEditor*)));
}

void Qt4Manager::editorChanged(Core::IEditor *editor)
{
    // Handle old editor
    Designer::FormWindowEditor *lastFormEditor = qobject_cast<Designer::FormWindowEditor *>(m_lastEditor);
    if (lastFormEditor) {
        disconnect(lastFormEditor, SIGNAL(changed()), this, SLOT(uiEditorContentsChanged()));

        if (m_dirty) {
            const QString contents = lastFormEditor->contents();
            foreach(Qt4Project *project, m_projects)
                project->rootProjectNode()->updateCodeModelSupportFromEditor(lastFormEditor->file()->fileName(), contents);
            m_dirty = false;
        }
    }

    m_lastEditor = editor;

    // Handle new editor
    if (Designer::FormWindowEditor *fw = qobject_cast<Designer::FormWindowEditor *>(editor))
        connect(fw, SIGNAL(changed()), this, SLOT(uiEditorContentsChanged()));
}

void Qt4Manager::editorAboutToClose(Core::IEditor *editor)
{
    if (m_lastEditor == editor) {
        // Oh no our editor is going to be closed
        // get the content first
        Designer::FormWindowEditor *lastEditor = qobject_cast<Designer::FormWindowEditor *>(m_lastEditor);
        if (lastEditor) {
            disconnect(lastEditor, SIGNAL(changed()), this, SLOT(uiEditorContentsChanged()));
            if (m_dirty) {
                const QString contents = lastEditor->contents();
                foreach(Qt4Project *project, m_projects)
                    project->rootProjectNode()->updateCodeModelSupportFromEditor(lastEditor->file()->fileName(), contents);
                m_dirty = false;
            }
        }
        m_lastEditor = 0;
    }
}

void Qt4Manager::uiEditorContentsChanged()
{
    // cast sender, get filename
    if (m_dirty)
        return;
    Designer::FormWindowEditor *fw = qobject_cast<Designer::FormWindowEditor *>(sender());
    if (!fw)
        return;
    m_dirty = true;
}

Core::Context Qt4Manager::projectContext() const
{
     return m_plugin->projectContext();
}

Core::Context Qt4Manager::projectLanguage() const
{
    return Core::Context(ProjectExplorer::Constants::LANG_CXX);
}

QString Qt4Manager::mimeType() const
{
    return QLatin1String(Qt4ProjectManager::Constants::PROFILE_MIMETYPE);
}

// Prototype Ui for update of QmlApplicationView files.
// TODO implement a proper Ui for this. Maybe not as modal message box.
// When removing this, also remove the inclusions of "wizards/qmlstandaloneapp.h" and QtGui/QMessageBox
inline void updateQmlApplicationViewerFiles(const QString proFile)
{
    const QList<GeneratedFileInfo> updates =
            QmlStandaloneApp::fileUpdates(proFile);
    if (!updates.empty()) {
        // TODO Translate the folloing strings when we want to keep the code
        QString message = QLatin1String("The following files are either outdated or have been modified:");
        message.append(QLatin1String("<ul>"));
        foreach (const GeneratedFileInfo &info, updates) {
            QStringList reasons;
            if (info.wasModified())
                reasons.append(QLatin1String("modified"));
            if (info.isOutdated())
                reasons.append(QLatin1String("outdated"));
            message.append(QString::fromLatin1("<li><nobr>%1 (%2)</nobr></li>")
                           .arg(QDir::toNativeSeparators(info.fileInfo.canonicalFilePath()))
                           .arg(reasons.join(QLatin1String(", "))));
        }
        message.append(QLatin1String("</ul>"));
        message.append(QLatin1String("Do you want Qt Creator to update the files? Any changes will be lost."));
        const QString title = QLatin1String("Update of the QmlApplicationView files");
        if (QMessageBox::question(0, title, message, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            QString error;
            if (!QmlStandaloneApp::updateFiles(updates, error))
                QMessageBox::critical(0, title, error);
        }
    }
}

ProjectExplorer::Project *Qt4Manager::openProject(const QString &fileName)
{
    Core::MessageManager *messageManager = Core::ICore::instance()->messageManager();

    // TODO Make all file paths relative & remove this hack
    // We convert the path to an absolute one here because qt4project.cpp
    // && profileevaluator use absolute/canonical file paths all over the place
    // Correct fix would be to remove these calls ...
    QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();

    if (canonicalFilePath.isEmpty()) {
        messageManager->printToOutputPane(tr("Failed opening project '%1': Project file does not exist").arg(QDir::toNativeSeparators(canonicalFilePath)));
        return 0;
    }

    foreach (ProjectExplorer::Project *pi, projectExplorer()->session()->projects()) {
        if (canonicalFilePath == pi->file()->fileName()) {
            messageManager->printToOutputPane(tr("Failed opening project '%1': Project already open").arg(QDir::toNativeSeparators(canonicalFilePath)));
            return 0;
        }
    }

    updateQmlApplicationViewerFiles(canonicalFilePath);

    Qt4Project *pro = new Qt4Project(this, canonicalFilePath);
    return pro;
}

ProjectExplorer::ProjectExplorerPlugin *Qt4Manager::projectExplorer() const
{
    return ProjectExplorer::ProjectExplorerPlugin::instance();
}

ProjectExplorer::Node *Qt4Manager::contextNode() const
{
    return m_contextNode;
}

void Qt4Manager::setContextNode(ProjectExplorer::Node *node)
{
    m_contextNode = node;
}

void Qt4Manager::setContextProject(ProjectExplorer::Project *project)
{
    m_contextProject = project;
}

ProjectExplorer::Project *Qt4Manager::contextProject() const
{
    return m_contextProject;
}

void Qt4Manager::runQMake()
{
    runQMake(projectExplorer()->currentProject(), 0);
}

void Qt4Manager::runQMakeContextMenu()
{
    runQMake(m_contextProject, m_contextNode);
}

void Qt4Manager::runQMake(ProjectExplorer::Project *p, ProjectExplorer::Node *node)
{
    if (!ProjectExplorer::ProjectExplorerPlugin::instance()->saveModifiedFiles())
        return;
    Qt4Project *qt4pro = qobject_cast<Qt4Project *>(p);
    QTC_ASSERT(qt4pro, return);

    if (!qt4pro->activeTarget() ||
        !qt4pro->activeTarget()->activeBuildConfiguration())
    return;

    Qt4BuildConfiguration *bc = qt4pro->activeTarget()->activeBuildConfiguration();
    QMakeStep *qs = bc->qmakeStep();

    if (!qs)
        return;
    //found qmakeStep, now use it
    qs->setForced(true);

    if (node != 0 && node != qt4pro->rootProjectNode())
        if (Qt4ProFileNode *profile = qobject_cast<Qt4ProFileNode *>(node))
            bc->setSubNodeBuild(profile);

    projectExplorer()->buildManager()->appendStep(qs);
    bc->setSubNodeBuild(0);
}

void Qt4Manager::buildSubDirContextMenu()
{
    handleSubDirContexMenu(BUILD);
}

void Qt4Manager::cleanSubDirContextMenu()
{
    handleSubDirContexMenu(CLEAN);
}

void Qt4Manager::rebuildSubDirContextMenu()
{
    handleSubDirContexMenu(REBUILD);
}

void Qt4Manager::handleSubDirContexMenu(Qt4Manager::Action action)
{
    Qt4Project *qt4pro = qobject_cast<Qt4Project *>(m_contextProject);
    QTC_ASSERT(qt4pro, return);

    if (!qt4pro->activeTarget() ||
        !qt4pro->activeTarget()->activeBuildConfiguration())
    return;

    Qt4BuildConfiguration *bc = qt4pro->activeTarget()->activeBuildConfiguration();
    if (m_contextNode != 0 && m_contextNode != qt4pro->rootProjectNode())
        if (Qt4ProFileNode *profile = qobject_cast<Qt4ProFileNode *>(m_contextNode))
            bc->setSubNodeBuild(profile);

    if (projectExplorer()->saveModifiedFiles()) {
        if (action == BUILD) {
            projectExplorer()->buildManager()->buildList(bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD));
        } else if (action == CLEAN) {
            projectExplorer()->buildManager()->buildList(bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_CLEAN));
        } else if (action == REBUILD) {
            QList<ProjectExplorer::BuildStepList *> stepLists;
            stepLists << bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_CLEAN);
            stepLists << bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
            projectExplorer()->buildManager()->buildLists(stepLists);
        }
    }

    bc->setSubNodeBuild(0);
}

QString Qt4Manager::fileTypeId(ProjectExplorer::FileType type)
{
    switch (type) {
    case HeaderType:
        return QLatin1String(qt4FileTypes[0]);
    case SourceType:
        return QLatin1String(qt4FileTypes[1]);
    case FormType:
        return QLatin1String(qt4FileTypes[2]);
    case ResourceType:
        return QLatin1String(qt4FileTypes[3]);
    case UnknownFileType:
    default:
        break;
    }
    return QString();
}
