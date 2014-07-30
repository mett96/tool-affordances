 /*
  * Copyright (C) 2013 RobotCub Consortium, European Commission FP6 Project IST-004370
  * Author:  Afonso Gonçalves
  * email:   agoncalves@isr.ist.utl.pt
  * website: www.robotcub.org
  * Permission is granted to copy, distribute, and/or modify this program
  * under the terms of the GNU General Public License, version 2 or any
  * later version published by the Free Software Foundation.
  *
  * A copy of the license can be found at
  * http://www.robotcub.org/icub/license/gpl.txt
  *
  * This program is distributed in the hope that it will be useful, but
  * WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
  * Public License for more details
  */

#ifndef SIMTOOLLOADERMODULE_H
#define SIMTOOLLOADERMODULE_H

#include <string>
#include <vector>
#include <yarp/os/all.h>
#include <yarp/sig/Vector.h>

//for iCart and iGaze
#include <yarp/dev/Drivers.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/ControlBoardInterfaces.h>
#include <yarp/dev/CartesianControl.h>

YARP_DECLARE_DEVICES(icubmod)

using namespace std;
using namespace yarp::os;

enum iCubArm {LEFT, RIGHT};
enum objType {BOX, SBOX, SPH, SSPH, CYL, SCYL, MODEL, SMODEL};

class SimObject {
protected:
    double positionX;
    double positionY;
    double positionZ;

    double rotationX;
    double rotationY;
    double rotationZ;

    double colorR;
    double colorG;
    double colorB;

public:
    int    objSubIndex;

    void setObjectPosition(double posx, double posy,  double posz);
    void setObjectRotation(double rotx, double roty,  double rotz);
    void setObjectColor   (double red,  double green, double blue);

    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true)  = 0;
    virtual Bottle   grabObjectBottle(iCubArm arm)                     = 0;
    virtual Bottle rotateObjectBottle()                                = 0;
    virtual Bottle deleteObject()                                      = 0; //no implementation yet
};

class SimBox: public SimObject {
private:
    double sizeX;
    double sizeY;
    double sizeZ;

public:
    SimBox(double posx, double posy, double posz,
           double rotx, double roty, double rotz,
           double red, double green, double blue,
           double sizx, double sizy, double sizz);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);

};

class SimSBox: public SimObject {
private:
    double sizeX;
    double sizeY;
    double sizeZ;

public:
    SimSBox(double posx, double posy, double posz,
            double rotx, double roty, double rotz,
            double red, double green, double blue,
            double sizx, double sizy, double sizz);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);
};

class SimSph: public SimObject {
private:
    double radius;

public:
    SimSph(double posx, double posy,  double posz,
           double rotx, double roty,  double rotz,
           double red,  double green, double blue,
           double rad);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);
};

class SimSSph: public SimObject {
private:
    double radius;

public:
    SimSSph(double posx, double posy,  double posz,
            double rotx, double roty,  double rotz,
            double red,  double green, double blue,
            double rad);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);
};

class SimCyl: public SimObject {
private:
    double radius;
    double height;

public:
    SimCyl(double posx, double posy,  double posz,
           double rotx, double roty,  double rotz,
           double red,  double green, double blue,
           double rad,  double hei);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);
};

class SimSCyl: public SimObject {
private:
    double radius;
    double height;

public:
    SimSCyl(double posx, double posy,  double posz,
            double rotx, double roty,  double rotz,
            double red,  double green, double blue,
            double rad,  double hei);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);
};

class SimModel: public SimObject {
private:
    ConstString mesh;
    ConstString texture;

public:
    SimModel(double posx, double posy, double posz,
             double rotx, double roty, double rotz,
             ConstString mes, ConstString tex);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);
};

class SimSModel: public SimObject {
private:
    ConstString mesh;
    ConstString texture;

public:
    SimSModel(double posx, double posy, double posz,
              double rotx, double roty, double rotz,
              ConstString mes, ConstString tex);
    virtual Bottle   makeObjectBottle(vector<int>& ind, bool collision = true);
    virtual Bottle deleteObject();
    virtual Bottle rotateObjectBottle();
    virtual Bottle   grabObjectBottle(iCubArm arm);
};

class SimWorld {
protected:

public:
    SimWorld();
    SimWorld(const Bottle& threadTable, vector<Bottle>& threadObject);
    Bottle deleteAll();
    void resetSimObjectIndex();

    SimObject *simTable;
    vector<SimObject*> simObject;
    vector<int> objSubIndex;
};

class CtrlThread:public RateThread {
protected:

    Port      *simToolLoaderCmdInputPort;
    RpcClient *simToolLoaderSimOutputPort;

    Bottle threadTable;
    vector<Bottle> threadObject;
    SimWorld simWorld;

    yarp::dev::PolyDriver					clientCart;
	yarp::dev::ICartesianControl			*icart;

public:
    CtrlThread(RpcClient *simToolLoaderSimOutput,
               Port *simToolLoaderCmdInput,
               const double period,
               const Bottle table,
               vector<Bottle> object);
    bool threadInit();
    void threadRelease();
    void run();
    void writeSim(Bottle cmd);
    void replyCmd(Bottle cmd);
};

class SimToolLoaderModule: public RFModule {
    string moduleName;

    string      simToolLoaderSimOutputPortName;
    RpcClient   simToolLoaderSimOutputPort;

    string      simToolLoaderCmdInputPortName;
    Port        simToolLoaderCmdInputPort;

    int threadPeriod;
    int numberObjs;

    CtrlThread *ctrlThread;

public:
    double getPeriod();
    bool configure(yarp::os::ResourceFinder &rf);
    bool updateModule();
    bool interruptModule();
    bool close();
};

#endif /* SIMOBJLOADERMODULE_H */
