#ifndef SYSTEMALERT_H
#define SYSTEMALERT_H

#include <QObject>

class SystemAlert : public QObject
{
    Q_OBJECT
public:
    explicit SystemAlert(QObject *parent = nullptr);
    
public slots:
    void playSound();
};

#endif // SYSTEMALERT_H
