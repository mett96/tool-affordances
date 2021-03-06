/*
 * AFFORNDACE MANAGER
 * Copyright (C) 2014 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Tanis Mar
 * email: tanis.mar@iit.it
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
#include <yarp/os/LogStream.h>

#include "tool3DManager.h"

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace yarp::dev;

// XXX XXX Remove disp from all the functions using it.

/**********************************************************
					PUBLIC METHODS
/**********************************************************/

/******************* RF overwrites ************************/
bool Tool3DManager::configure(ResourceFinder &rf)
{
    printf("Configuring module...\n");

    name = rf.check("name",Value("tool3DManager")).asString().c_str();
    camera = rf.check("camera",Value("left")).asString().c_str();
    hand = rf.check("hand",Value("right")).asString().c_str();
    robot = rf.check("robot",Value("icub")).asString().c_str();

    yInfo() << "Configuring module name " << name << ", camera " << camera << ", hand "<< hand << ", on robot " << robot;

    if (robot=="icubSim"){
        yInfo() << "Configuring for robot icubSim";
        tableHeight = rf.check("tableHeight", Value(-0.13)).asDouble();      // Height of the table in the robots coord frame
    }else{
        yInfo() << "Configuring for robot real iCub";
        tableHeight = rf.check("tableHeight", Value(-0.13)).asDouble();      // Height of the table in the robots coord frame
    }
    

    // Attach server port to read RPC commands via thrift
    attach(rpcCmd);

    // Read and load the tool's models
    Bottle temp;
    string modelName = "obj";

    yInfo() << "Loading models to buffer";
    bool noMoreModels = false;
    int n =0;
    while (!noMoreModels){      // read until there are no more objects.
        stringstream s;
        s.str("");
        s << modelName << n;
        string objNameNum = s.str();
        temp = rf.findGroup("objects").findGroup(objNameNum);

        if(!temp.isNull()) {
            yInfo() << "model " << n << ", id: " << objNameNum;            
            string modelx = temp.get(2).asString();         // retrieve model name with format
            string::size_type idx;                          // remove format (.x) to save only tool name
            idx = modelx.rfind('.');                        // ..
            string model = modelx.substr(0,idx);            // format removed.
            yInfo() << ", model: " << model;

            models.push_back(model);
            temp.clear();
            n++;
        }else {
            noMoreModels = true;
        }
    }
    int numberObjs = n;
    yInfo() << "Loaded " << numberObjs << " objects. ";


    // Module flow parameters
    running = true;
    toolLoadedIdx = -1;
    trackingObj = false;
    toolname = "";
    seg2D = true;

    imgW = 320;
    imgH = 240;
    bbsize = 100;

    // Initialize effect measuring vectors
    target3DcoordsIni.resize(3, 0.0);
    target3DcoordsAfter.resize(3, 0.0);
    target3DrotIni.resize(3, 0.0);
    target3DrotAfter.resize(3, 0.0);
    effectVec.resize(3, 0.0);

	//ports
	bool ret = true;  
    ret = ret && matlabPort.open(("/"+name+"/matlab:i").c_str());                     // port to receive data from MATLAB processing
    ret = ret && trackPort.open(("/"+name+"/track:i").c_str());                     // port to receive data from MATLAB processing
    ret = ret && affDataPort.open(("/"+name+"/affData:o").c_str());                   // port to send data of computed affordances out for recording

    if (!ret){
		printf("Problems opening ports\n");
		return false;
	}

    //rpc
    bool retRPC = true; 
    retRPC = rpcCmd.open(("/"+name+"/rpc:i").c_str());					   			   // rpc in client to receive commands
    retRPC = retRPC && rpcAreCmd.open(("/"+name+"/are:rpc").c_str());                  // rpc server to query ARE for cmds
    retRPC = retRPC && rpcAreGet.open(("/"+name+"/are:get").c_str());                  // rpc server to query ARE for gets
    retRPC = retRPC && rpcSimToolLoader.open(("/"+name+"/simToolLoader:rpc").c_str()); // rpc server to query the simtooloader module
    retRPC = retRPC && rpcSimulator.open(("/"+name+"/simulator:rpc").c_str());         // rpc server to query the simulator
    retRPC = retRPC && rpcAffMotor.open(("/"+name+"/affMotor:rpc").c_str());           // rpc server to query Affordance Motor Module
    retRPC = retRPC && rpc3Dexp.open(("/"+name+"/toolinc:rpc").c_str());              // rpc server to query toolIncorporator module
    retRPC = retRPC && rpcObjFinder.open(("/"+name+"/objFind:rpc").c_str());           // rpc server to query objectFinder
    retRPC = retRPC && rpcGraspClass.open(("/"+name+"/graspClass:rpc").c_str());           // rpc server to query graspClassifier
    retRPC = retRPC && rpcToolClass.open(("/"+name+"/toolClass:rpc").c_str());           // rpc server to query graspClassifier


	if (!retRPC){
		printf("Problems opening rpc ports\n");
		return false;
	}

    printf("Manager configured correctly \n");

	return true;
}


/**********************************************************/
bool Tool3DManager::updateModule()
{
	if (!running)
        return false;
	return true;
}

/**********************************************************/
bool Tool3DManager::interruptModule()
{    
    affDataPort.interrupt();    
    matlabPort.interrupt();
    trackPort.interrupt();

    rpcCmd.interrupt();
    rpcSimToolLoader.interrupt();
    rpcSimulator.interrupt();
    rpcAreCmd.interrupt();
    rpcAffMotor.interrupt();
    rpcGraspClass.interrupt();
    rpcToolClass.interrupt();

    rpc3Dexp.interrupt();
    rpcObjFinder.interrupt();

    return true;
}

/**********************************************************/
bool Tool3DManager::close()
{
    affDataPort.close();
    matlabPort.close();
    trackPort.close();

    rpcCmd.close();
    rpcSimToolLoader.close();
    rpcSimulator.close();
    rpcAreCmd.close();
    rpcAffMotor.close();
    rpcGraspClass.close();
    rpcToolClass.close();

    rpc3Dexp.close();
    rpcObjFinder.close();

	running = false;

    return true;
}

/**********************************************************/
double Tool3DManager::getPeriod()
{
    return 0.1;
}

/***********************************************************/
/****************** RPC HANDLING METHODS *******************/

/* Atomic commands */
/***********************************************************************************/
// ======================== Module control commands
bool Tool3DManager::attach(yarp::os::RpcServer &source)
{
    return this->yarp().attachAsServer(source);
}

bool Tool3DManager::start(){
    yInfo() << "Starting!";
	running = true;
	return true;
}

bool Tool3DManager::quit(){
    yInfo() << "Quitting!";
	running = false;
	return true;
}

/***********************************************************************************/
// ======================== Tool Load and Info Extraction

bool Tool3DManager::loadModel(const string &tool){
    bool ok = load3Dmodel(tool);
    return ok;
}

string Tool3DManager::graspTool(const string &tool){    
    string tool_loaded;
    if (robot=="icubSim"){
        yWarning() << "Tool in sim is loaded, not grasped.";
        tool_loaded = "not_loaded";
    }else{
        tool_loaded = graspToolExe(tool);
    }
    return tool_loaded;
}

bool Tool3DManager::getToolParam(const string &tool, double deg, double disp, double tilt, double shift){
    bool ok;
    if (robot=="icubSim"){
        yInfo() << "Loading tool " << tool << " in simulation";
        ok = getToolSimExe(tool, deg, disp, tilt);
    }else{
        yInfo() << "Getting tool " << tool << " in real robot, finding pose by param.";
        ok = getToolParamExe(tool, deg, disp, tilt, shift);
    }
    return ok;
}

bool Tool3DManager::getToolAlign(const string &tool){
    bool ok;
    if (robot=="icubSim"){
        yError() << "Grasp params need to be given to grasp on simulator. Try getToolByPose.";
    }else{
        ok = getToolAlignExe(tool);
    }
    return ok;
}


bool Tool3DManager::explore(const string &tool, const string &exp_mode){
    bool ok;
    ok = exploreTool(tool, exp_mode, tooltip);

    yInfo() <<  "Tool loaded and pose and tooltip found at (" <<tooltip.x <<", " << tooltip.y << "," <<tooltip.z <<  ") !";

    ok = addToolTip(tooltip);
    if (!ok){
        yError() << "Tool tip could not be attached.";
        return false;
    }
    yInfo() <<  "Tooltip attached, ready to perform action!";
    return ok;
}

bool Tool3DManager::lookTool(){
    bool ok;
    ok = lookToolExe();
    return ok;
}

bool Tool3DManager::regrasp(double deg, double disp, double tilt, double shift){
    bool ok;
    if (toolname == ""){
        yError() << "A tool has to be loaded before regrasping is possible";
        return false;
    }

    ok = regraspExe(tooltip, deg, disp, tilt, shift);
    return ok;
}

bool Tool3DManager::findPose(){

    if (robot=="icubSim"){
        yError() << "Grasp in sim corresponds surely with given one.";
        return false;
    }
    bool ok;
    double ori, displ, tilt;
    ok = findPoseAlignExe(tooltip, ori, displ, tilt);
    if (!ok){
        yError() << "Tool Pose could not be estimated properly";
        return false;
    }
    yInfo() <<  "Tool  grasped with orientation: " << ori <<", displ: " << displ << " and tilt " << tilt;
    yInfo() <<  "Tooltip found at (" <<tooltip.x <<", " << tooltip.y << "," <<tooltip.z <<  ") !";

    ok = addToolTip(tooltip);
    if (!ok){
        yError() << "Tool tip could not be attached.";
        return false;
    }
    yInfo() <<  "Tooltip attached, ready to perform action!";


    return true;
}

bool Tool3DManager::getToolFeats(){
    extractFeats();
    return true;
}

bool Tool3DManager::learn(const std::string &label)
{
    yInfo() << " Received 'learn' " << label;
    return trainGraspClas(label);
}

bool Tool3DManager::check()
{
    return classify();
}



/***********************************************************************************/
// ======================== Functions to call actions - compute effects

bool Tool3DManager::goHome(bool hands){
    goHomeExe(hands);
    return true;
}

bool Tool3DManager::findTable(bool calib){

    double th = findTableHeight(calib);
    tableHeight = th;
    yInfo() << "Setting table height to " << th;
    return true;
}

bool Tool3DManager::slide(double theta, double radius){
    return slideExe(theta,radius);
}

bool Tool3DManager::drag3D(double x, double y, double z, double theta, double radius, double tilt, bool useTool){
    return drag3DExe(x, y, z, theta, radius, tilt, useTool);
}

bool Tool3DManager::drag(double theta, double radius, double tilt){
    return dragExe(theta,radius, tilt);
}

bool Tool3DManager::trackObj(){
    return trackObjExe();
}

bool Tool3DManager::compEff(){
    return computeEffect();
}

bool Tool3DManager::resetObj(){
    return resetCube();
}

/***********************************************************************************/
//  ======================== Functions to get object info:
Vector Tool3DManager::objLoc(){
    Vector loc;
    loc.clear();         // Clear to make space for new rotation values
    loc.resize(3);
    getObjLoc(loc);

    return loc;
}

Vector Tool3DManager::objRot(){
    Vector rot;
    rot.clear();         // Clear to make space for new rotation values
    rot.resize(3);
    getObjRot(rot);

    return rot;
}


/***********************************************************************************/
//  ======================== Exploration commands  =======================
bool Tool3DManager::runRandPoses(int numPoses,int numAct){
    double thetaDiv = 360.0/numAct;
    double theta = 0.0;
    Rand randG; // YARP random generator

    int tilt = 0;
    if (!(robot=="icubSim")){
        tilt = -15; // Action on the real robot need some tilt to not crash teh hand on the table
    }


    int toolI_prev = round(randG.scalar(1,52));
    for (int p=1 ; p <= numPoses ; p++){
        int toolI;

        double  graspOr = randG.scalar(-90,90);
        double  graspDisp = randG.scalar(-3,3);
        double  graspTilt = randG.scalar(0,45);

        yInfo() << "Starting trial with orientation "<< graspOr <<", displacement "<<  graspDisp << " and tilt " << graspTilt << ".";

        string tool = models[toolI];
        getToolParam(tool, graspOr, graspDisp, graspTilt);  // re-grasp the tool with the given grasp parameters
        extractFeats();

        for (int i=1 ; i<=numAct ; i++){
            dragExe(theta,0.15,tilt);
            computeEffect();
            theta += thetaDiv;
        }
        toolI_prev = toolI;
    }
    return true;
}



bool Tool3DManager::runToolPose(int numRep,  int numAct){
    double thetaDiv = 360.0/numAct;
    double theta = 0.0;

    int tilt = 0;
    if (robot!="icubSim"){
        tilt = -15; // Action on the real robot need some tilt to not crash the hand on the table
    }

    // We assume the tool is grasped previously with getTool() or getToolByPose()
    for (int r=1 ; r<=numRep ; r++){
        for (int i=1 ; i<=numAct ; i++){
            dragExe(theta, 0.15, tilt);
            computeEffect();
            yInfo() << "Going to next action";
            theta += thetaDiv;
        }
        theta = 0.0;
    }
    return true;
}

bool Tool3DManager::runToolTrial(int numRep, const string &tool,  int numAct){


    // For each tool, run all combinations of
    // x 3 Grasps: left front right
    // x 8 thetas (every 45 degrees).
    for ( int ori = -90; ori < 100; ori = ori + 90){            // This is a loop for {-90, 0, 90}

        yInfo() << "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
        // Get tool
        if (robot == "icubSim"){
            getToolSimExe(tool,ori);
        }else{
            yInfo() << "Hand me the tool: " << tool << " at orientation "<< ori << ".";
            getToolAlignExe(tool);
        }

        // Perform exploration actions
        runToolPose(numRep, numAct);
    }
    return true;
}

bool Tool3DManager::runExp(int toolIni, int toolEnd)
{
    for (int toolI=toolIni ; toolI<= toolEnd ; toolI++)
    {        
        string tool = models[toolI];
        runToolTrial(1, tool);
    }
    return true;
}

//  ======================== Exploitation commands  =======================
bool Tool3DManager::selectAction(int goal)
{   // Goals are 1 to achieve maximum displacement, 2 to perform pull
    int tilt = 0;
    if (!(robot=="icubSim")){
        tilt = -15; // Action on the real robot need some tilt to not crash teh hand on the table
        goHomeExe(false);
    }

    // A tool has to have been handled in the first place -> so the model is already loaded    
    // Extract features to send them to MATLAB.
    if (!extractFeats()){
        yError() << "ToolFeatExt coudln't extract the features.";
        return false;
    }
    yInfo() << " Oriented 3D features extracted and sent to Matlab";

    Bottle *matReply = matlabPort.read(true);
    yInfo() << " Read Bottle from Matlab of length " << matReply->size();
    yInfo() << " Read prediction form Matlab " << matReply->toString().c_str();

    // Find the maximum for affPred and perform doAction on the max index.
    //Bottle *effPred = matReply->get(0).asList();
    //yInfo() << ", Predicted Effect Vector " << effPred->toString().c_str();

    if (goal == 1) // Goal is to achieve maximum displacement
    {
        int numPoint = matReply->size();
        double bestEff = 0;
        int bestAngleI = 0;
        for ( int angleI = 0; angleI < numPoint; angleI++ ) // Find the approach that will generate predicted maximum effect
        {
           double predEff = matReply->get(angleI).asDouble();
           if (predEff > bestEff){
               bestEff = 	predEff;
               bestAngleI = angleI;
           }
        }
        double theta = (360/numPoint) * bestAngleI;     // Get the drag angle corresponding to the best predicted effect
        printf("Best predicted drag from angle %g, predicted effect of %f m  \n", theta, bestEff);

        dragExe(theta,0.15, tilt);

        return computeEffect();
    }
    if (goal == 2) // Goal is to pull the object, if possible
    {
        // Pull action is dragging towards the robot -> angle 270 -> actionI = 6
        int pullI = 6;
        double predEff = matReply->get(pullI).asDouble();
        if (predEff < 0.05) // If the expected pull is smaller than 5 cm means teh tool can't afford pulling{
        {
            yError() << "Please give me another tool, I can't pull with this tool";
            return false;
        }
        yInfo() << "I can pull with this tool, expected drag is "<< predEff;
        dragExe(270, 0.15, tilt);
        return computeEffect();
    }else{
        yInfo() << "Please select a goal (1:Max disp, 2: pull)";
        return false;
    }
}

bool Tool3DManager::predExp(int toolini, int goal)
{   // Remember that MATLAB has to be connected to YARP, and the right affordance models loaded.

    int numTools_test = 50;
    // List the tools to use for testing:
    if (robot == "icubSim"){
        for (int testToolI = toolini; testToolI < numTools_test; testToolI++ ){
            for ( int ori = -90; ori < 100; ori = ori + 90){            // This is a loop for {-90, 0, 90}            
                string& tool = models[testToolI];       // Because tools are 1 indexed on the file, but 0 on sim
                yInfo() << "Attempting to load " << tool;
                if (getToolParam(tool, ori)){
                    yInfo() << "Tool loaded, selecting best action for goal " << goal;
                    for (int i = 1; i<3;i++){                                // this is just repetitions.
                        selectAction(goal);
                    }
                }else{
                    yError() << "Couldn't load the desired test tool";
                    return false;
                }

            }
        }
        return true;

    }else{


    }

}


/***********************************************************************************/
// ========================  CONFIGURATION FUNTIONS


bool Tool3DManager::setSeg(bool seg){
    Bottle cmd3DE,reply3DE;                 // bottles for toolIncorporator
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("setSeg");
    if (seg){
        cmd3DE.addString("ON");
    }else{
        cmd3DE.addString("OFF");
    }
    yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    rpc3Dexp.write(cmd3DE, reply3DE);
    yInfo() << "RPC reply from toolIncorporator: " << reply3DE.toString() << ".";
    if (reply3DE.get(0).asString() != "[ack]" ){
        yError() << "toolIncorporator couldnt set segmentatio to ON/OFF";
        return false;
    }

    return true;
}



/**********************************************************/
                    /* PRIVATE METHODS */
/**********************************************************/


// ===================================================================================================================================/
/**************************************************** TOOL LOAD AND INFORMATION ******************************************************/
// ===================================================================================================================================/


/**********************************************************/
bool Tool3DManager::load3Dmodel(const string &cloudName)
{
    // Communicates with toolIncorporator to load the corresponding Model
    Bottle cmd3DE,reply3DE;                 // bottles for toolIncorporator
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("loadCloud");
    cmd3DE.addString(cloudName);
    yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    rpc3Dexp.write(cmd3DE, reply3DE);

    if (reply3DE.get(0).asString() == "[ack]" ){
        toolname = cloudName;
        yInfo() << "Cloud " << cloudName << " loaded";
        return true;
    }else {
        yError() << "toolIncorporator coudln't load the tool.";
        yError() << "Check the spelling of the name, or, if the tool has no known model, try 'explore (tool)'.";
        return false;
    }
}


/**********************************************************/
string Tool3DManager::graspToolExe(const std::string &tool)
{
    Bottle cmdAMM, replyAMM;               // bottles for Affordance Motor Module
    Bottle cmdARE, replyARE;               // bottles for actionsRenderingEngine
    Bottle cmd3DE, reply3DE;                 // bottles for toolIncorporator

    // Remove any end effector extension that might be.
    cmdAMM.clear();replyAMM.clear();
    cmdAMM.addString("tool");
    cmdAMM.addString("remove");
    rpcAffMotor.write(cmdAMM, replyAMM);

    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("cleartool");
    rpc3Dexp.write(cmd3DE, reply3DE);

    // Send commands to ARE to get the tool, close the hand and go to central position
    fprintf(stdout,"Reach me a tool please.\n");
    cmdARE.clear();
    replyARE.clear();
    cmdARE.addString("tato");
    cmdARE.addString(hand);
    rpcAreCmd.write(cmdARE,replyARE);
    //Time::delay(2);

    // Close hand on tool grasp
    cmdARE.clear();
    replyARE.clear();
    cmdARE.addString("hand");
    cmdARE.addString("close_hand_tool");
    cmdARE.addString(hand);
    rpcAreCmd.write(cmdARE, replyARE);

    // Check if grasp was successful
    cmdARE.clear();
    replyARE.clear();
    cmdARE.addString("get");
    cmdARE.addString("holding");
    rpcAreCmd.write(cmdARE, replyARE);
    if(!replyARE.get(0).asBool())
        return "not_loaded";

    string tool_name = tool;
    // If no tool name is given, query O3DE to load model 3D Pointcloud.
    if (tool == "unknown"){
        cmd3DE.clear();   reply3DE.clear();
        cmd3DE.addString("recog");
        rpc3Dexp.write(cmd3DE, reply3DE);

        yInfo() << "Reply from Classifier: " << reply3DE.toString();
        if (reply3DE.get(0).asString() != "[ack]" ){
            yError() << "Couldn't recognize the tool.";
            return "not_loaded";
        }
        tool_name = reply3DE.get(1).asString();
        yInfo() << "Tool recognized as " << tool_name;
    }

    if (!load3Dmodel(tool_name)){
        yError() << "Coudln't load the tool.";
        return "not_loaded" ;
    }

    //goHomeExe();

    return tool_name;
}


/**********************************************************/
bool Tool3DManager::lookToolExe()
{
    // Communicates with toolIncorporator to load the corresponding Model, and find its pose and tooltip
    Bottle cmd3DE,reply3DE;                 // bottles for toolFeatExt
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("lookAtTool");
    yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    rpc3Dexp.write(cmd3DE, reply3DE);

    if (reply3DE.get(0).asString() != "[ack]" ){
        yError() << "toolIncorporator coudln't look at tool.";
        return false;
    }

    return true;
}


/**********************************************************/
bool Tool3DManager::getToolSimExe(const string &tool, const double graspOr,const double graspDisp, const double graspTilt)
{    

    int toolI = findToolInd(tool);
    if (toolI < 0){     yError() << "Tool label not found";        return false;}
    yInfo()<< "Grasping tool " << tool << " with index " << toolI;

    yInfo() << "Tool present: " << toolname <<".";

    Bottle cmdAMM,replyAMM;               // bottles for the Affordance Motor Module
    Bottle cmdSim,replySim;               // bottles for Simulator
    Bottle cmd3DE,reply3DE;               // bottles for toolFeatExt

    // Remove any end effector extension that might be.
    cmdAMM.clear();replyAMM.clear();
    cmdAMM.addString("tool");
    cmdAMM.addString("remove");
    rpcAffMotor.write(cmdAMM, replyAMM);

    // Move hand to center to receive tool on correct position - implemented by faking a push action to the center to avoid iCart dependencies.
    yInfo() << "Moving "<< hand << " arm to a central position";
    double dispY = (hand=="right")?0.15:-0.15;
    cmdAMM.clear();replyAMM.clear();
    cmdAMM.addString("push");            // Set a position in the center in front of the robot
    cmdAMM.addDouble(-0.25);
    cmdAMM.addDouble(dispY);
    cmdAMM.addDouble(0.05);
    cmdAMM.addDouble(0.0);       // No angle
    cmdAMM.addDouble(0.0);       // No radius
    rpcAffMotor.write(cmdAMM, replyAMM);

    double tiltValid = graspTilt;
    if (graspTilt > 90.0) {   tiltValid  = 90.0; }
    if (graspTilt < 0.0)  {   tiltValid  = 0.0; }
    yInfo()<<"Getting tool " << toolI <<" with orientation: "<< graspOr << ", displacement: " << graspDisp << "and tilt: " << tiltValid;


    if (tool != toolname)   // A new tool is being loaded
    {
        // Call simtoolloader to clean the world
        cmdSim.clear();   replySim.clear();
        cmdSim.addString("del");        
        rpcSimToolLoader.write(cmdSim, replySim);

        // Call simtoolloader to create the tool
        cmdSim.clear();   replySim.clear();
        cmdSim.addString("tool");
        cmdSim.addInt(toolI);               // tool
        cmdSim.addInt(0);                   // object -> Cube
        cmdSim.addInt(graspOr);             // orientation
        cmdSim.addInt(graspDisp);           // displacement
        cmdSim.addInt(tiltValid);           // tilt
        rpcSimToolLoader.write(cmdSim, replySim);
        yInfo() << "Sent RPC command to simtoolloader: " << cmdSim.toString() << ".";

        // check succesful tool creation
        if (replySim.size() <1){
            yError() << "simtoolloader couldn't load tool.";
            return false;
        }

        yInfo() << "Loading cloud model: " << tool  << " in O3DE.";

        // Query toolFeatExt to load model to load 3D Pointcloud.
        if (!load3Dmodel(tool)){
            yError() << "Coudln't load the tool.";
            return false;
        }        

    } else {        // Need just to reGrasp tool

        // Call simtoolloader to create the tool
        cmdSim.clear();   replySim.clear();
        cmdSim.addString("rot");
        cmdSim.addDouble(graspOr);             // orientation
        cmdSim.addDouble(graspDisp);           // displacement
        cmdSim.addDouble(tiltValid);           // tilt
        rpcSimToolLoader.write(cmdSim, replySim);
        yInfo() << "Sent RPC command to simtoolloader: " << cmdSim.toString() << ".";

        // check succesful tool reGrasping
        if (replySim.size() <1){
            yError() << "simtoolloader cloudln't load tool.";
            return false;
        }
    }

    // Get the tooltip canonical coordinates wrt the hand coordinate frame from its bounding box.
    yInfo() << "Getting tooltip coordinates.";
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("findTooltipParam");
    cmd3DE.addDouble(graspOr);
    cmd3DE.addDouble(graspDisp);
    cmd3DE.addDouble(tiltValid);
    rpc3Dexp.write(cmd3DE, reply3DE);
    yInfo() << "Sent RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    if (reply3DE.get(0).asString() != "[ack]" ){
        yError() << "toolIncorporator coudln't compute the tooltip.";
        return false;
    }
    yInfo() << " Received reply: " << reply3DE.toString();

    Bottle tooltipBot = reply3DE.tail();
    tooltip.x = tooltipBot.get(0).asDouble();
    tooltip.y = tooltipBot.get(1).asDouble();
    tooltip.z = tooltipBot.get(2).asDouble();

    yInfo() << "Tooltip of tool in positon: x= " << tooltip.x << ", y = " << tooltip.y << ", z = " << tooltip.z;

    yInfo() << "Attaching tooltip.";
    addToolTip(tooltip);
    yInfo() << "Tool loaded and tooltip attached";

    // Put action parameters on a port so they can be read
    graspVec.clear();		// Clear to make space for new coordinates
    graspVec.resize(4);     // Resize to save orientation - displacement coordinates coordinates
    graspVec[0] = toolI;
    graspVec[1] = graspOr;
    graspVec[2] = graspDisp;
    graspVec[3] = tiltValid;

    return true;
}


/**********************************************************/
bool Tool3DManager::getToolParamExe(const string &tool, const double graspOr, const double graspDisp, const double graspTilt, const double graspShift)
{
    string tool_loaded;
    tool_loaded = graspToolExe(tool);
    if (tool_loaded == "not_loaded"){
        yError() << "iCub couldn't grasp the tool properly";
        return false;
    }
    int toolI = findToolInd(tool_loaded);

    yInfo() <<  "Tool  "<< tool_loaded << " grasped!";

    bool ok;
    // Query toolIncorporator to load model to load 3D Pointcloud.
    yInfo() << "Querying objectd3DExplorer to set tooltip based on the folllowing parameters.";
    yInfo() << " -- orientation: "<< graspOr << " -- displacement: " << graspDisp << " -- tilt: " << graspTilt  << " -- shift: " << graspShift;
    ok = findPoseParamExe(tooltip, graspOr, graspDisp, graspTilt, graspShift);
    if (!ok){
        yError() << "Tool Tip could not be computed from params.";
        return false;
    }
    yInfo() << "Tooltip of tool in positon: x= "<< tooltip.x << ", y = " << tooltip.y << ", z = " << tooltip.z;

    yInfo() << "Attaching tooltip.";
    ok = addToolTip(tooltip);
    if (!ok){
        yError() << "Tool tip could not be attached.";
        return false;
    }
    yInfo() <<  "Tooltip attached, ready to perform action!";


    // Put action parameters on a port so they can be read
    graspVec.clear();		// Clear to make space for new coordinates
    graspVec.resize(4);     // Resize to save orientation - displacement coordinates coordinates
    graspVec[0] = toolI;
    graspVec[1] = graspOr;
    graspVec[2] = graspDisp;
    graspVec[3] = graspTilt;
    return true;
}

/**********************************************************/
bool Tool3DManager::getToolAlignExe(const string& tool)
{
    string tool_loaded;
    tool_loaded = graspToolExe(tool);
    if (tool_loaded == "not_loaded"){
        yError() << "iCub couldn't grasp the tool properly";
        return false;
    }
    int toolI = findToolInd(tool_loaded);

    bool ok;
    double ori, displ, tilt;
    ok = findPoseAlignExe(tooltip, ori, displ, tilt);
    if (!ok){
        yError() << "Tool Pose could not be estimated properly";
        return false;
    }
    yInfo() <<  "Tool  grasped with orientation: " << ori <<", displ: " << displ << " and tilt " << tilt;
    yInfo() <<  "Tooltip found at (" <<tooltip.x <<", " << tooltip.y << "," <<tooltip.z <<  ") !";

    ok =addToolTip(tooltip);
    if (!ok){
        yError() << "Tool tip could not be attached.";
        return false;
    }
    yInfo() <<  "Tooltip attached, ready to perform action!";

    // Put action parameters on a port so they can be read
    graspVec.clear();		// Clear to make space for new coordinates
    graspVec.resize(4);     // Resize to save orientation - displacement coordinates coordinates
    graspVec[0] = toolI;
    graspVec[1] = ori;
    graspVec[2] = displ;
    graspVec[3] = tilt;
    return true;
}



/**********************************************************/
bool Tool3DManager::regraspExe(Point3D &newTip, const double graspOr, const double graspDisp, const double graspTilt, const double graspShift)
    {

    int toolI = findToolInd(toolname);
    if (toolI < 0){     yError() << "Tool label not found";        return false;}
    yInfo()<< "Regrasping tool " << toolname << " with index " << toolI;

    // Query simtoolloader to rotate the virtual tool
    yInfo()<< "with orientation: " << graspOr << ", disp " << graspDisp << ", Tilt " << graspTilt << " and shift " << graspShift;

    // Call simtoolloader to create the tool
    if (robot == "icubSim"){
        Bottle cmdSim,replySim;       // bottles for Simulator
        cmdSim.clear();   replySim.clear();
        cmdSim.addString("rot");
        cmdSim.addDouble(graspOr);             // orientation
        cmdSim.addDouble(graspDisp);           // displacement
        cmdSim.addDouble(graspTilt);           // displacement
        rpcSimToolLoader.write(cmdSim, replySim);
        yInfo() << "Sent RPC command to simtoolloader: " << cmdSim.toString() << ".";

        // check succesful tool reGrasping
        if (replySim.size() <1){
            yError() << "simtoolloader clouldn't regrasp tool.";
            return false;
        }
        //yInfo() << "Received reply: " << replySim.toString();
    }

    // Transform the tooltip from the canonical to the current grasp
    bool ok;
    ok = findPoseParamExe(newTip, graspOr, graspDisp, graspTilt, graspShift);
    if (!ok){
        yError() << "Tool tip could not be computed from params.";
        return false;
    }
    yInfo() << "Tooltip of tool in positon: x= "<< newTip.x << ", y = " << newTip.y << ", z = " << newTip.z;

    yInfo() << "Attaching tooltip.";
    ok = addToolTip(newTip);
    if (!ok){
        yError() << "Tool tip could not be attached.";
        return false;
    }
    fprintf(stdout,"Tool loaded and tooltip attached \n");

    // Put action parameters on a port so they can be read
    graspVec.clear();		// Clear to make space for new coordinates
    graspVec.resize(4);     // Resize to save orientation - displacement coordinates coordinates
    graspVec[0] = toolI;
    graspVec[1] = graspOr;
    graspVec[2] = graspDisp;
    graspVec[3] = graspTilt;

    return true;
}



/**********************************************************/
bool Tool3DManager::findPoseAlignExe(Point3D &ttip, double &ori, double &displ, double &tilt)
{
    // Query toolIncorporator to find the tooltip
    Bottle cmd3DE,reply3DE;
    yInfo() << "Finding out Pose and tooltip by aligning 3D partial view with model.";
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("findTooltipAlign");
    cmd3DE.addInt(5);
    yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    rpc3Dexp.write(cmd3DE, reply3DE);
    yInfo() << "RPC reply from toolIncorporator: " << reply3DE.toString() << ".";
    if (reply3DE.get(0).asString() == "[nack]" ){
        yError() << "toolIncorporator couldn't find out the pose.";
        return false;
    }

    Bottle tooltipBot = reply3DE.tail();
    ttip.x = tooltipBot.get(0).asDouble();
    ttip.y = tooltipBot.get(1).asDouble();
    ttip.z = tooltipBot.get(2).asDouble();
    ori = tooltipBot.get(3).asDouble();
    displ = tooltipBot.get(4).asDouble();
    tilt = tooltipBot.get(5).asDouble();

    yInfo() <<  "Pose found relative to parameters:" << " orientation: " << ori <<", displ: " << displ << " and tilt " << tilt;

    return true;
}



/**********************************************************/
bool Tool3DManager::findPoseParamExe(Point3D &ttip, const double graspOr, const double graspDisp, const double graspTilt, const double graspShift)
{
    // Query toolIncorporator to find the tooltip
    yInfo() << "Finding out tooltoltip from model and grasp parameters.";
    Bottle cmd3DE, reply3DE;

    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("findTooltipParam");
    cmd3DE.addDouble(graspOr);
    cmd3DE.addDouble(graspDisp);
    cmd3DE.addDouble(graspTilt);
    cmd3DE.addDouble(graspShift);
    yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    rpc3Dexp.write(cmd3DE, reply3DE);
    yInfo() << "RPC reply from toolIncorporator: " << reply3DE.toString() << ".";
    if (reply3DE.get(0).asString() != "[ack]" ){
        yError() << "toolIncorporator couldn't estimate the tip.";
        return false;
    }
    Bottle ttBot = reply3DE.tail();

    ttip.x = ttBot.get(0).asDouble();
    ttip.y = ttBot.get(1).asDouble();
    ttip.z = ttBot.get(2).asDouble();

    return true;
}


/**********************************************************/
bool Tool3DManager::exploreTool(const string &tool,const string &exp_mode, Point3D &ttip)
{
    // Query toolIncorporator to find the tooltip
    yInfo() << "Finding out tooltoltip by exploration and symmetry";
    Bottle cmd3DE, reply3DE;

    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("exploreTool");
    cmd3DE.addString(tool);
    cmd3DE.addString(exp_mode);
    yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    rpc3Dexp.write(cmd3DE, reply3DE);
    yInfo() << "RPC reply from toolIncorporator: " << reply3DE.toString() << ".";
    if (reply3DE.get(0).asString() != "[ack]" ){
        yError() << "There was some error exploring the tool.";
        return false;
    }

    // In case the exploration whas 2D and no model was loaded.
    if (exp_mode == "2D"){
        load3Dmodel(tool);
        ttip.x = 0.17;
        ttip.y = -0.17;
        ttip.z = 0.0;
    }else{


        cmd3DE.clear();   reply3DE.clear();
        cmd3DE.addString("makecanon");
        yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
        rpc3Dexp.write(cmd3DE, reply3DE);
        yInfo() << "RPC reply from toolIncorporator: " << reply3DE.toString() << ".";
        if (reply3DE.get(0).asString() != "[ack]" ){
            yError() << "Main planes could not be computed.";
            return false;
        }

        cmd3DE.clear();   reply3DE.clear();
        cmd3DE.addString("findTooltipSym");
        yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
        rpc3Dexp.write(cmd3DE, reply3DE);
        yInfo() << "RPC reply from toolIncorporator: " << reply3DE.toString() << ".";
        if (reply3DE.get(0).asString() != "[ack]" ){
            yError() << "There was some estimating the tooltip.";
            return false;
        }

        Bottle ttBot = reply3DE.tail();

        ttip.x = ttBot.get(0).asDouble();
        ttip.y = ttBot.get(1).asDouble();
        ttip.z = ttBot.get(2).asDouble();
    }
    return true;
}


/**********************************************************/
int Tool3DManager::findToolInd(const string& tool)
{
    int toolI = -1;
    vector<string>::iterator it = std::find(models.begin(), models.end(), tool);
    if ( it == models.end() ) { //Not found
        return -1;
    }else{                          // Found
        toolI = std::distance(models.begin(), it);                  // return position of found element
    }
    return toolI;
}

double Tool3DManager::findOri()
{
    Bottle cmd3DE,reply3DE;                 // bottles for toolIncorporator
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("getOri");
    rpc3Dexp.write(cmd3DE, reply3DE);
    if (reply3DE.get(0).asString() != "[ack]" ){
        yError() << "toolIncorporator coudln't return tool's orientation.";
        return false;
    }
    double ori = reply3DE.get(0).asDouble();

    return ori;
}

/**********************************************************/
bool Tool3DManager::addToolTip(const Point3D ttip)
{
    Bottle cmdAMM, replyAMM;

    // Attach the new tooltip to the "body schema"
    cmdAMM.clear();replyAMM.clear();
    cmdAMM.addString("tool");
    cmdAMM.addString("attach");
    cmdAMM.addString(hand.c_str());
    cmdAMM.addDouble(ttip.x);
    cmdAMM.addDouble(ttip.y);
    cmdAMM.addDouble(ttip.z);
    yInfo() << "RPC command to affMotor: " << cmdAMM.toString();
    rpcAffMotor.write(cmdAMM, replyAMM);
    yInfo() << "RPC reply from affMotor: " << replyAMM.toString();
    if (!(replyAMM.toString() == "[ack]")){
        yError() <<  "Affordance Motor Module could not add the tooltip ";
        return false;
    }

    return true;
}

/**********************************************************/
bool Tool3DManager::extractFeats()
{    // Query toolFeatExt to extract features
    yInfo() << "Extacting features of handled tool.";
    Bottle cmd3DE,reply3DE;                 // bottles for toolIncorporator

    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("extractFeats");
    yInfo() << "Sending RPC command to toolIncorporator: " << cmd3DE.toString() << ".";
    rpc3Dexp.write(cmd3DE, reply3DE);
    yInfo() << "RPC reply from toolIncorporator: " << reply3DE.toString() << ".";

    return true;
}

bool Tool3DManager::trainGraspClas(const std::string &label)
{
    yInfo() << " Sending 'observe' command to ARE ";
    Bottle cmdARE, replyARE;
    cmdARE.clear();	replyARE.clear();
    cmdARE.addString("observe");
    rpcAreCmd.write(cmdARE,replyARE);

    yInfo() << " Getting hand reference frame from ARE get.";

    Bottle cmdAREget, replyAREget;
    cmdAREget.clear();	replyAREget.clear();
    cmdAREget.addString("get");
    cmdAREget.addString("hand");
    cmdAREget.addString("image");
    rpcAreGet.write(cmdAREget,replyAREget);

    yInfo() << " ARE replied: " << replyAREget.toString();

    int hand_u_left = replyAREget.get(0).asInt();
    int hand_v_left = replyAREget.get(1).asInt();

    yInfo() << " Hand ref frame at : " << hand_u_left << ", " << hand_v_left;

    //int hand_u_left = replyAREget.get(0).asList()->get(0).asInt();
    //int hand_v_left = replyAREget.get(0).asList()->get(1).asInt();


    Vector BB(4,0.0);
    BB[0] = hand_u_left - bbsize/2;         // tlx
    if (BB[0]< 0){        BB[0]= 0;    }
    BB[1] = hand_v_left- bbsize/2;         // tly
    if (BB[1]< 0){        BB[1]= 0;    }
    BB[2] = hand_u_left + bbsize/2;         // brx
    if (BB[2]> imgW){        BB[2]= imgW;    }
    BB[3] = hand_v_left + bbsize/2;         // bry
    if (BB[3]> imgH){        BB[3]= imgH;    }

    yInfo() << " Bounding Box is: " << BB[0] <<", " << BB[1] <<", " << BB[2] <<", " << BB[3];


    Bottle cmdClas, replyClas;
    cmdClas.clear();	replyClas.clear();
    cmdClas.addString("train");
    cmdClas.addString(label);
    cmdClas.addInt(BB[0]);
    cmdClas.addInt(BB[1]);
    cmdClas.addInt(BB[2]);
    cmdClas.addInt(BB[3]);
    rpcGraspClass.write(cmdClas,replyClas);

    yInfo() << " Classifier trained with label " << label;

    return true;
}

bool Tool3DManager::classify()
{
    yInfo() << " Sending 'observe' command to ARE ";
    Bottle cmdARE, replyARE;
    cmdARE.clear();	replyARE.clear();
    cmdARE.addString("observe");
    rpcAreCmd.write(cmdARE,replyARE);

    yInfo() << " Getting hand reference frame from ARE get.";

    Bottle cmdAREget, replyAREget;
    cmdAREget.clear();	replyAREget.clear();
    cmdAREget.addString("get");
    cmdAREget.addString("hand");
    cmdAREget.addString("image");
    rpcAreGet.write(cmdAREget,replyAREget);

    yInfo() << " ARE replied: " << replyAREget.toString();

    int hand_u_left = replyAREget.get(0).asInt();
    int hand_v_left = replyAREget.get(1).asInt();

    yInfo() << " Hand ref frame at : " << hand_u_left << ", " << hand_v_left;

    //int hand_u_left = replyAREget.get(0).asList()->get(0).asInt();
    //int hand_v_left = replyAREget.get(0).asList()->get(1).asInt();

    Vector BB(4,0.0);
    BB[0] = hand_u_left - bbsize/2;         // tlx
    if (BB[0]< 0){        BB[0]= 0;    }
    BB[1] = hand_v_left- bbsize/2;         // tly
    if (BB[1]< 0){        BB[1]= 0;    }
    BB[2] = hand_u_left + bbsize/2;         // brx
    if (BB[2]> imgW){        BB[2]= imgW;    }
    BB[3] = hand_v_left + bbsize/2;         // bry
    if (BB[3]> imgH){        BB[3]= imgH;    }

    yInfo() << " Bounding Box is: " << BB[0] <<", " << BB[1] <<", " << BB[2] <<", " << BB[3];

    Bottle cmdClas, replyClas;
    cmdClas.clear();	replyClas.clear();
    cmdClas.addString("check");
    cmdClas.addInt(BB[0]);
    cmdClas.addInt(BB[1]);
    cmdClas.addInt(BB[2]);
    cmdClas.addInt(BB[3]);
    rpcGraspClass.write(cmdClas,replyClas);

    yInfo() << " Classifier replied: " << replyClas.toString();

    bool answer;
    if (strcmp (replyClas.toString().c_str(),"[ok]") == 0)
        answer = true;
    else
        answer = false;

    //answer = false;

    return answer;

}


// ===================================================================================================================================/
/***************************************************   OBJECT LOCALIZATION   *********************************************************/
// ===================================================================================================================================/

/**********************************************************/
double Tool3DManager::findTableHeight(bool calib){

    Bottle cmdARE, replyARE;
    if (calib){
        yInfo() << "Asking ARE to calibrate on table: " << hand;

        cmdARE.clear();    replyARE.clear();
        cmdARE.addString("calib");
        cmdARE.addString("table");
        cmdARE.addString("left");
        rpcAreCmd.write(cmdARE,replyARE);
    }

    yInfo() << "Retrieving table height data from ARE " << hand;
    cmdARE.clear();     replyARE.clear();
    cmdARE.addString("get");
    cmdARE.addString("table");
    rpcAreGet.write(cmdARE,replyARE);

    double th = replyARE.get(0).asList()->get(1).asDouble();

    yInfo() << "Table height is " << th;

    return th;
}


/**********************************************************/
bool Tool3DManager::trackObjExe()
{
    goHomeExe(false);
    // Select the target object to be tracked
    printf("\n \n Click first TOP LEFT and then BOTTOM RIGHT from the object to set the tracking template. Please.\n");
    Bottle cmdFinder,replyFinder;
    cmdFinder.clear();
    replyFinder.clear();
    cmdFinder.addString("getBox");
    //fprintf(stdout,"RPC to objFinder %s:\n", cmdFinder.toString().c_str());
    rpcObjFinder.write(cmdFinder, replyFinder);
    //fprintf(stdout,"  Reply from objFinder %s:\n",replyFinder.toString().c_str());

    printf("Object template has been set properly\n");
    trackingObj = true;

    // Set ARE to constantly track the object
    /*Bottle cmdAre, replyAre;
    cmdAre.addString("track");
    cmdAre.addString("track");
    cmdAre.addString("no_sacc");
    rpcAreCmd.write(cmdAre,replyAre);
    fprintf(stdout,"tracking started %s:\n",replyAre.toString().c_str());
    */
    return true;
}

/**********************************************************/
bool Tool3DManager::getObjLoc(Vector &coords3D)
{
    if (robot=="icubSim"){
        Bottle cmdSim,replySim;
        yInfo() <<"Getting 3D coords of object.";
        cmdSim.clear();        replySim.clear();
        cmdSim.addString("world");
        cmdSim.addString("get");
        cmdSim.addString("box");
        cmdSim.addInt(1);
        //printf("RPC to simulator: %s \n", cmdSim.toString().c_str());
        rpcSimulator.write(cmdSim, replySim);
        //printf("  Reply from simulator: %s \n", replySim.toString().c_str());

        if (replySim.size() >1){
            Vector coords3Dworld(3,0.0);
            // REad coordinates returned by simualtor
            coords3Dworld(0) = replySim.get(0).asDouble();
            coords3Dworld(1) = replySim.get(1).asDouble();
            coords3Dworld(2) = replySim.get(2).asDouble();
            printf("Point in 3D in world coordinates retrieved: %g, %g %g\n", coords3Dworld(0), coords3Dworld(1), coords3Dworld(2));

            // Transform object coordinates from World to robot!!!
            coords3D(0) = -(coords3Dworld(2) + 0.026);        // Xr = -Zw + 0.026 m
            coords3D(1) = -coords3Dworld(0);                // Yr = -Xw
            coords3D(2) = coords3Dworld(1) - 0.5976;        // Zr = Yw - 0.5976 m
            printf("Point in 3D in robot coordinates: %g, %g %g\n", coords3D(0), coords3D(1), coords3D(2));

            return true;
        }
        yInfo() << "No 3D point received";
        return false;

    }else{

        // Get the 2D coordinates of the object from objectFinder
        Bottle *trackCoords = trackPort.read(true);
        if (trackCoords->size() <1){
            yError() << "No 2D point received";
            return false;
        }

        yInfo() << " Read from tracker: " << trackCoords->toString();
        double tlx = trackCoords->get(0).asList()->get(0).asDouble();
        double tly = trackCoords->get(0).asList()->get(1).asDouble();
        double brx = trackCoords->get(0).asList()->get(2).asDouble();
        double bry = trackCoords->get(0).asList()->get(3).asDouble();

        Vector coords2D(2,0.0);
        coords2D(0) = (tlx + brx)/2;
        coords2D(1) = (tly + bry)/2;

        yInfo() << "Object tracked at 2D coordinates (" << coords2D[0] << ", " << coords2D[1] << ")";

        // XXX change this to call to IOLreaching calibration to get a relaible 3D point for object
        coords3D.resize(3);
        Bottle cmdAre, replyAre;
        cmdAre.addString("get");
        cmdAre.addString("s2c");
        Bottle& boords_bot = cmdAre.addList();
        boords_bot.addDouble(coords2D[0]);
        boords_bot.addDouble(coords2D[1]);
        rpcAreCmd.write(cmdAre,replyAre);


        if (replyAre.size() >1){
            coords3D(0) = replyAre.get(0).asDouble();
            coords3D(1) = replyAre.get(1).asDouble();
            coords3D(2) = replyAre.get(2).asDouble();
            printf("Point in 3D retrieved: %g, %g %g\n", coords3D(0), coords3D(1), coords3D(2));

            return true;
        }
        yInfo() << "No 3D point received";
        return false;
    }
}

/**********************************************************/
bool Tool3DManager::getObjRot(Vector &rot3D)
{
    if (robot=="icubSim"){
        Bottle cmdSim,replySim;
        //fprintf(stdout,"Get 3D coords of tracked object:\n");
        cmdSim.clear();        replySim.clear();
        cmdSim.addString("world");
        cmdSim.addString("rot");
        cmdSim.addString("box");
        cmdSim.addInt(1);
        //printf("RPC to simulator: %s \n", cmdSim.toString().c_str());
        rpcSimulator.write(cmdSim, replySim);
        //printf("  Reply from simulator: %s \n", replySim.toString().c_str());

        if (replySim.size() >1){
            rot3D(0) = replySim.get(0).asDouble();
            rot3D(1) = replySim.get(1).asDouble();
            rot3D(2) = replySim.get(2).asDouble();
            printf("Rotation in 3D retrieved: %g, %g %g\n", rot3D(0), rot3D(1), rot3D(2));
            return true;
        }
        yError() << "No 3D point received";
        return false;

    }else{

        return false;
    }
}


/**********************************************************/
bool Tool3DManager::computeEffect()
{
    yInfo() << "Computing effect from action." ;

    effectVec.clear();		// Clear to make space for new coordinates
    effectVec.resize(3);    // Resize to 3D coordinates

    // Get the actual coordinates
    target3DcoordsAfter.clear();		// Clear to make space for new coordinates
    target3DcoordsAfter.resize(3);      // Resize to 3D coordinates
    target3DrotAfter.clear();         // Clear to make space for new rotation values
    target3DrotAfter.resize(3);
    if(!getObjLoc(target3DcoordsAfter))     // Get the location of the object after the action
    {
        yError() << " Object not located, cant observe effect";
        return false;
    }
    yInfo() << "Coords after action: (" << target3DcoordsAfter[0] << ", " << target3DcoordsAfter[1] << ", "<< target3DcoordsAfter[2] << "). ";

    //To compute the displacement, we assume that the object hasnt move in the z axis (that is, has remained on the table)
    Vector displ = target3DcoordsAfter - target3DcoordsIni;
    double dx = displ[0];
    double dy = displ[1];

    double displDist = sqrt(dx*dx+dy*dy);  //sum of the squares of the differences
    double displAngle = atan2 (dy,dx) * 180 / M_PI;

    // Put values into the effect Vector
    effectVec[0] = displDist;
    effectVec[1] = displAngle;

    if (robot=="icubSim"){
        if(!getObjRot(target3DrotAfter))        // Get the rotation of the object after the action
        {
            yError() << " Object rotation not available, cant compute effect";
            return false;
        }
        yInfo() << "Rotation after action: (" << target3DrotAfter[0] << ", " << target3DrotAfter[2] << ", "<< target3DrotAfter[2] << "). ";

        //To compute the rotation, we assume that the object has only rotated around axis Y (that is, has not been tipped)
        Vector rot = target3DrotAfter - target3DrotIni;
        double effectRot = rot[1];  // Rotation difference around Y axis
        effectVec[2] = effectRot;
    } else {
        effectVec[2] = 0.0;
    }

    yInfo() << "Object displaced " << effectVec[0] << " meters on " << effectVec[1] << " direction, and rotated " << effectVec[2] << " degrees.";

    // Prepare the affordance out bottle
    aff_out_data.clear();
    Bottle& aff_toolpose = aff_out_data.addList();
    aff_toolpose.addString(toolname);
    aff_toolpose.addInt(graspVec[0]);           // toolI
    aff_toolpose.addDouble(graspVec[1]);        // orientation
    Bottle& aff_action = aff_out_data.addList();
    aff_action.addDouble(actVec[0]);        // theta
    aff_action.addDouble(actVec[1]);        // radius
    Bottle& aff_effect = aff_out_data.addList();
    aff_effect.addDouble(effectVec[0]);        // distance
    aff_effect.addDouble(effectVec[1]);        // angle
    aff_effect.addDouble(effectVec[2]);        // rotation

    // Send all data so it can be read and saved.
    sendAffData();

    resetCube();

    return true;
}

bool Tool3DManager::resetCube()
{
    if (robot=="icubSim"){
        // Put the cube back in place to restart round:
        Bottle cmdSim,replySim;       // bottles for Simulator
        cmdSim.clear();   replySim.clear();
        cmdSim.addString("move");
        cmdSim.addInt(0);                   // object -> Cube
        rpcSimToolLoader.write(cmdSim, replySim); // Call simtoolloader to create the tool
        //yInfo() << "Sent RPC command to simtoolloader: " << cmdSim.toString() << ".";
        //yInfo() << " Received reply: " << replySim.toString();
        Time::delay(0.3); // give time for the cube to reach its position before jumping to next step
    } else {
        yInfo() << '\n';
        yInfo() << "Effect computed, 5 seconds to put the object back in place";
        yInfo() << "5";
        Time::delay(1);
        yInfo() << "4";
        Time::delay(1);
        yInfo() << "3";
        Time::delay(1);
        yInfo() << "2";
        Time::delay(1);
        yInfo() << "1";
        Time::delay(1);
    }
    return true;
}

bool Tool3DManager::sendAffData()
{
    // Send (action - grasp - effect) parameters together so that they are synced

    // put values on a port so they can be read
    affDataPort.write(aff_out_data);

    return true;
}


// ===================================================================================================================================/
/******************************************************   ACTION EXECUTION   *********************************************************/
// ===================================================================================================================================/

/**********************************************************/
void Tool3DManager::goHomeExe(const bool hands)
{
    yInfo() << "Going home, hands: " << hands;

    if (robot == "icubSim"){
        Bottle cmd3DE,reply3DE;                 // bottles for O3DE
        cmd3DE.clear();   reply3DE.clear();
        cmd3DE.addString("showTipProj");
        cmd3DE.addString("OFF");
        rpc3Dexp.write(cmd3DE, reply3DE);

        Bottle cmdAMM, replyAMM;
        // Move hand to center to receive tool on correct position - implemented by faking a push action to the center to avoid iCart dependencies.
        yInfo() << "Moving "<< hand << " arm to a central position";
        double dispY = (hand=="right")?0.20:-0.20;
        cmdAMM.clear();replyAMM.clear();
        cmdAMM.addString("push");            // Set a position in the center in front of the robot
        cmdAMM.addDouble(-0.50);
        cmdAMM.addDouble(dispY);
        cmdAMM.addDouble(0.10);
        cmdAMM.addDouble(0.0);       // No angle
        cmdAMM.addDouble(0.0);       // No radius
        rpcAffMotor.write(cmdAMM, replyAMM);

        cmd3DE.clear();   reply3DE.clear();
        cmd3DE.addString("showTipProj");
        cmd3DE.addString("ON");
        rpc3Dexp.write(cmd3DE, reply3DE);


    }else{
        Bottle cmdAre, replyAre;
        cmdAre.clear();
        replyAre.clear();
        if (hands){
            cmdAre.addString("home");
            cmdAre.addString("all");
        }else{
            cmdAre.addString("home");
            cmdAre.addString("head");
            cmdAre.addString("arms");
        }
        rpcAreCmd.write(cmdAre,replyAre);
    }
    return;
}


/**********************************************************/
bool Tool3DManager::slideExe(const double theta, const double radius)
{
    actVec.clear();		// Clear to make space for new coordinates
    actVec.resize(2);   // Resize to save theta - radius coordinates coordinates

    yInfo()<< "Performing slide action from angle " << theta <<" and radius "<< radius ;
    target3DcoordsIni.clear();		// Clear to make space for new coordinates
    target3DcoordsIni.resize(3);    // Resizze to 3D coordinates
    target3DrotIni.clear();         // Clear to make space for new rotation values
    target3DrotIni.resize(3);

    // Locate object and perform slide action with given theta and aradius parameters
    if (!getObjLoc(target3DcoordsIni))
    {
        yError() << " Object not located, cant perform action";
        return false;
    }
    getObjRot(target3DrotIni);               // Get the initial rotation of the object

    // Action during the Affordance Motor Module execution transforms the end-effector from the hand to the tip pf the tool,
    // so the representation has to be inverted so it still shows the right tooltip while performing the action
    // and restored afterwards so it shows it also during not execution.
    Point3D tooltip_tmp= tooltip;
    tooltip.x = 0.0;    tooltip.y = 0.0;  tooltip.z = 0.0;


    yInfo() << "Approaching to coords: (" << target3DcoordsIni[0] << ", " << target3DcoordsIni[2] << ", "<< target3DcoordsIni[2] << "). ";
    Bottle cmdAMM,replyAMM;                    // bottles for the Affordance Motor Module
    cmdAMM.clear();replyAMM.clear();
    cmdAMM.addString("slide");                 // Set a position in the center in front of the robot
    cmdAMM.addDouble(target3DcoordsIni[0]);
    cmdAMM.addDouble(target3DcoordsIni[1]);
    cmdAMM.addDouble(target3DcoordsIni[2] + 0.03);   // Approach the center of the object, not its lower part.
    cmdAMM.addDouble(theta);
    cmdAMM.addDouble(radius);
    //fprintf(stdout,"RPC to affMotor: %s\n",cmdAMM.toString().c_str());
    rpcAffMotor.write(cmdAMM, replyAMM);
    //fprintf(stdout,"  Reply from affMotor: %s\n",replyAMM.toString().c_str());


    // Restore show tool coordinates
    tooltip = tooltip_tmp;

    // Put action parameters on a port so they can be read
    actVec[0] = theta;
    actVec[1] = radius;

    goHomeExe();

    return true;
}

/**********************************************************/
bool Tool3DManager::dragExe(const double theta, const double radius, const double tilt)
{

    goHomeExe();

    Bottle cmdAMM,replyAMM;                    // bottles for the Affordance Motor Module
    Bottle cmdARE,replyARE;
    Bottle cmd3DE,reply3DE;                         // bottles for O3DE
    actVec.clear();		// Clear to make space for new coordinates
    actVec.resize(2);   // Resize to save theta - radius coordinates coordinates

    yInfo()<< "Performing drag action from angle " << theta <<" and radius "<< radius ;
    target3DcoordsIni.clear();		// Clear to make space for new coordinates
    target3DcoordsIni.resize(3);    // Resizze to 3D coordinates
    target3DrotIni.clear();         // Clear to make space for new rotation values
    target3DrotIni.resize(3);



    // Locate object and perform slide action with given theta and aradius parameters
    if (!getObjLoc(target3DcoordsIni))
    {
        yError() << " Object not located, cant perform action";
        return false;
    }
    getObjRot(target3DrotIni);               // Get the initial rotation of the object

    if (robot != "icubSim"){        
        bool coordOK = checkSaveDrag(target3DcoordsIni, theta, radius);       // check if the desired drag is save (heurisitcs).

        if (!coordOK){
            yError() << "Object coordenated out of save reach";
            return false;
        }
    }

    // Action during the Affordance Motor Module execution transforms the end-effector from the hand to the tip pf the tool,
    // so the representation has to be inverted so it still shows the right tooltip while performing the action
    // and restored afterwards so it shows it also during not execution.
    yInfo() << "Deactivating Tootip projection: "; 

    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("showTipProj");
    cmd3DE.addString("OFF");
    rpc3Dexp.write(cmd3DE, reply3DE);

    if (robot != "icubSim"){
        cmdARE.clear();
        replyARE.clear();
        cmdARE.addString("look");
        cmdARE.addString("hand");
        cmdARE.addString(hand);
        cmdARE.addString("fixate");
        cmdARE.addString("block_eyes");
        cmdARE.addDouble(5.0);
        rpcAreCmd.write(cmdARE, replyARE);
    }

    // Perform drag action
    yInfo() << "Approaching to object on coords: (" << target3DcoordsIni[0] << ", " << target3DcoordsIni[1] << ", "<< target3DcoordsIni[2] << "). ";

    // Calculate approach shift depending on side it is approaching from:
    double toolOri = graspVec[1];
    double Y_off = 0.03*(sin(toolOri));


    cmdAMM.clear();replyAMM.clear();
    cmdAMM.addString("drag");                 // Set a position in the center in front of the robot
    cmdAMM.addDouble(target3DcoordsIni[0] - 0.05);   // Approach the end effector slightly behind the object to grab it properly
    cmdAMM.addDouble(target3DcoordsIni[1]+Y_off);
    cmdAMM.addDouble(target3DcoordsIni[2]);   // Approach the center of the object, not its lower part.
    cmdAMM.addDouble(theta);
    cmdAMM.addDouble(radius);
    cmdAMM.addDouble(tilt);
    rpcAffMotor.write(cmdAMM, replyAMM);

    // Restore show tool coordinates
    yInfo() << "Reactivating Tootip projection: ";
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("showTipProj");
    cmd3DE.addString("ON");
    rpc3Dexp.write(cmd3DE, reply3DE);

    // Put action parameters on a vector so they can be sent
    actVec[0] = theta;
    actVec[1] = radius;

    goHomeExe();

    return true;
}



/**********************************************************/
bool Tool3DManager::drag3DExe(double x, double y, double z, double theta, double radius, double tilt, bool useTool)
{
    yInfo()<< "Performing drag3D action from angle " << theta <<" and radius "<< radius  <<", tilt:"<< tilt;
    Bottle cmdAMM,replyAMM;                    // bottles for the Affordance Motor Module
    Bottle cmdARE,replyARE;


    if (!useTool){
         yInfo()<<"Removing tool";
        // Remove any end effector so that action is carried wrt hand reference frame.
        cmdAMM.clear();replyAMM.clear();
        cmdAMM.addString("tool");
        cmdAMM.addString("remove");
        rpcAffMotor.write(cmdAMM, replyAMM);
    }

    // Action during the Affordance Motor Module execution transforms the end-effector from the hand to the tip pf the tool,
    // so the representation has to be inverted so it still shows the right tooltip while performing the action
    // and restored afterwards so it shows it also during not execution.
    //Point3D tooltip_tmp = tooltip;
    //tooltip.x = 0.0;    tooltip.y = 0.0;  tooltip.z = 0.0;

    yInfo() << "Deactivating Tootip projection: "; 
    Bottle cmd3DE,reply3DE;                 // bottles for O3DE
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("showTipProj");
    cmd3DE.addString("OFF");
    rpc3Dexp.write(cmd3DE, reply3DE);

    yInfo() << "Approaching to object on coords: (" << x << ", " << y << ", "<< z << "). ";

    // Close hand on tool grasp
    if (robot != "icubSim"){
        cmdARE.clear();
        replyARE.clear();
        cmdARE.addString("look");
        cmdARE.addString("hand");
        cmdARE.addString(hand);
        cmdARE.addString("fixate");
        cmdARE.addString("block_eyes");
        cmdARE.addDouble(5.0);
        rpcAreCmd.write(cmdARE, replyARE);
    }

    cmdAMM.clear();replyAMM.clear();
    cmdAMM.addString("drag");                 // Set a position in the center in front of the robot
    cmdAMM.addDouble(x);   // Approach the end effector slightly behind the object to grab it properly
    cmdAMM.addDouble(y);
    cmdAMM.addDouble(z);   // Approach the center of the object, not its lower part.
    cmdAMM.addDouble(theta);
    cmdAMM.addDouble(radius);
    cmdAMM.addDouble(tilt);
    rpcAffMotor.write(cmdAMM, replyAMM);

    // Stop head moving for further visual processing
    if (robot != "icubSim"){
        cmdARE.clear();
        replyARE.clear();
        cmdARE.addString("idle");
        rpcAreCmd.write(cmdARE, replyARE);
    }

    // Restore show tool coordinates
    yInfo() << "Reactivating Tootip projection: ";
    cmd3DE.clear();   reply3DE.clear();
    cmd3DE.addString("showTipProj");
    cmd3DE.addString("ON");
    rpc3Dexp.write(cmd3DE, reply3DE);

    // Re-attach the tooltip for further actions
    if (!useTool){
        cmdAMM.clear();replyAMM.clear();
        cmdAMM.addString("tool");
        cmdAMM.addString("attach");
        cmdAMM.addString(hand.c_str());
        cmdAMM.addDouble(tooltip.x);
        cmdAMM.addDouble(tooltip.y);
        cmdAMM.addDouble(tooltip.z);
        rpcAffMotor.write(cmdAMM, replyAMM);
        if (!(replyAMM.toString() == "[ack]")){
            yError() <<  "Aff Motor Module could not re-attach the tooltip ";
            return false;
        }
    }

    goHomeExe();

    return true;
}

bool Tool3DManager::checkSaveDrag(Vector coords, double theta , double radius)
{
    // Define general workspace
    bool coordOK = true;
    if ((coords[0] < -0.6) || (coords[0] > -0.3)){
        coordOK = false;
    }
    if ((coords[1] < 0.15) || (coords[1] > 0.4)){
        coordOK = false;
    }
    if ((coords[2] < -0.17) || (coords[2] > 0.0)){
        coordOK = false;
    }

    // Check limit for specific actions
//    if (theta > 180) {
//        if (coords[0] > -0.)
//        {
//            coordOK = false;
//        }
//    }
    return coordOK;
}

//++++++++++++++++++++++++++++++ MAIN ++++++++++++++++++++++++++++++++//
/**********************************************************************/
int main(int argc, char *argv[])
{
    Network yarp;
    if (!yarp.checkNetwork())
        return -1;

    ResourceFinder rf;
    rf.setVerbose(true);
    // rf.setDefault("name","tool3DManager");
    //rf.setDefault("camera","left");
    rf.setDefault("robot","icub");
    //rf.setDefault("hand","right");
    rf.setDefaultContext("AffordancesProject");
    rf.setDefaultConfigFile("realTools.ini");
    //rf.setDefault("tracking_period","30");
    rf.configure(argc,argv);

    Tool3DManager tool3DManager;
    return tool3DManager.runModule(rf);
}
