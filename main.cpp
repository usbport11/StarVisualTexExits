#include "stdafx.h"
#include "classes/system/Shader.h"
#include "classes/system/Scene.h"
#include "classes/system/FPSController.h"
#include "classes/level/Leaf.h"
#include "classes/delaunay/delaunay.h"
#include "classes/level/TileMap.h"
#include "classes/buffers/AtlasBuffer.h"
#include "classes/image/TextureLoader.h"
#include "classes/buffers/TextureQuadBuffer.h"
#include "classes/buffers/StaticBuffer.h"

bool Pause;
bool keys[1024] = {0};
int WindowWidth = 800, WindowHeight = 600;
bool EnableVsync = 1;
GLFWwindow* window;
stFPSController FPSController;

int TilesCount[2] = {30, 30};
glm::vec2 Edge(2, 2);
glm::vec2 TileSize(20, 20);
glm::vec2 MouseSceneCoord;

MShader Shader;
MShader LineShader;
MStaticBuffer LinesBuffer;
MStaticBuffer QuadBuffer;
MScene Scene;

unsigned int txAtlas_cnt;
stTexture* txAtlas;
MAtlasBuffer AtlasBuffer;
MTileMap TileMap;
MTextureLoader TextureLoader;
glm::vec2 StartPoint;
glm::vec2 EndPoint;

bool FillTileBuffer() {
	unsigned int AtasPos[2];
	for(int i=0; i<TilesCount[0] - 1; i++) {
		for(int j=0; j<TilesCount[1] - 1; j++) {
			if(TileMap.GetValue(i, j) == TT_NONE) continue;
			if(TileMap.GetValue(i, j) == TT_FLOOR) {
				AtasPos[0] = 0;
				AtasPos[1] = 1;
			}
			if(TileMap.GetValue(i, j) == TT_WALL_FULL) {
				AtasPos[0] = 1;
				AtasPos[1] = 0;
			}
			if(TileMap.GetValue(i, j) == TT_WALL_PART) {
				AtasPos[0] = 0;
				AtasPos[1] = 0;
			}
			if(!AtlasBuffer.AddData(glm::vec2(i * TileSize.x, j * TileSize.y), glm::vec2((i + 1) * TileSize.x, (j + 1) * TileSize.y), AtasPos[0], AtasPos[1], 0, true)) return false;
		}
	}
	return true;
}

bool GenerateLevel() {
	QuadBuffer.Clear();
	LinesBuffer.Clear();
	AtlasBuffer.Clear();
	TileMap.Clear();
	
	list<TNode<stLeaf>* > Tree;
	int MinLeafSize = 6;
	int MaxLeafSize = 20;
	int MinRoomSize = 3;
	
	//create tree
	if(MinRoomSize >= MinLeafSize || MinLeafSize >= MaxLeafSize) {
		cout<<"Wrong settings"<<endl;
		return 0;
	}
	if(!SplitTree(&Tree, TilesCount[0], TilesCount[1], MinLeafSize, MaxLeafSize)) return false;
	
	//create rooms and fill centers map
	glm::vec3 Color = glm::vec3(1, 1, 1);
	TNode<NRectangle2>* pRoomNode;
	map<glm::vec2, TNode<NRectangle2>*, stVec2Compare> NodesCenters;
	glm::vec2 Center;
	NRectangle2* pRoom;
	list<TNode<stLeaf>* >::iterator itl;
	int RoomsNumber = 0;
	for(itl = Tree.begin(); itl != Tree.end(); itl++) {
		pRoomNode = CreateRoomInLeaf(*itl, MinRoomSize);
		if(!pRoomNode) continue;
		pRoom = pRoomNode->GetValueP();
		if(!pRoom) continue;
		//add in map
		Center.x = (pRoom->Position.x + pRoom->Size.x * 0.5) * TileSize.x;
		Center.y = (pRoom->Position.y + pRoom->Size.y * 0.5) * TileSize.y;
		NodesCenters.insert(pair<glm::vec2, TNode<NRectangle2>* >(Center, pRoomNode));
		//add in buffer
		RoomsNumber ++;
		TileMap.SetRectangle(*pRoom, 1);
	}
	if(RoomsNumber < 2) {
		cout<<"Too few rooms: "<<RoomsNumber<<endl;
		return false;
	}
	
	//copy centers for triangulation 
	//get start and end level points
	EndPoint = StartPoint = NodesCenters.begin()->first; //start point
	float Distance, MaxDistance = 0;
	map<glm::vec2, TNode<NRectangle2>*, stVec2Compare>::iterator itm;
	vector<glm::vec2> CentersPoints;
	for(itm = NodesCenters.begin(); itm != NodesCenters.end(); itm++) {
		CentersPoints.push_back(itm->first);
		Distance = glm::distance(itm->first, StartPoint);
		if(Distance > MaxDistance) {
			MaxDistance = Distance;
			EndPoint = itm->first;
		}
	}
	//rnd swap of points
	if(rand() % 2) {
		glm::vec2 Save = StartPoint;
		StartPoint = EndPoint;
		EndPoint = Save;
		cout<<"Swap points"<<endl;
		//at start point must be no monsters
	}
	
	//triangulate by delaunay and get mst
	MDelaunay Triangulation;
	vector<MTriangle> Triangles = Triangulation.Triangulate(CentersPoints);
	vector<MEdge> Edges = Triangulation.GetEdges();
	vector<MEdge> MST = Triangulation.CreateMSTEdges();
	
	//create halls
	//fill lines
	vector<NRectangle2> Halls;
	TNode<NRectangle2>* pNode0;
	TNode<NRectangle2>* pNode1;
	for(int i=0; i<MST.size(); i++) {
		pNode0 = NodesCenters[MST[i].p1];
		pNode1 = NodesCenters[MST[i].p2];
		Halls = CreateHalls2(pNode0->GetValueP(), pNode1->GetValueP());
		for(int k=0; k<Halls.size(); k++) {
			TileMap.SetRectangle(Halls[k], 1);
		}
		LinesBuffer.AddVertex(MST[i].p1, glm::vec3(1, 0, 0));
		LinesBuffer.AddVertex(MST[i].p2, glm::vec3(1, 0, 0));
	}
	Halls.clear();
	LinesBuffer.Dispose();
	
	//before clear all nodes data need calculate place of start and end of level
	//at start just create lines array
	QuadBuffer.AddVertex(StartPoint, glm::vec3(0, 1, 0));
	QuadBuffer.AddVertex(StartPoint + glm::vec2(20, 0), glm::vec3(0, 1, 0));
	QuadBuffer.AddVertex(StartPoint + glm::vec2(20, 20), glm::vec3(0, 1, 0));
	QuadBuffer.AddVertex(StartPoint + glm::vec2(0, 20), glm::vec3(0, 1, 0));
	QuadBuffer.AddVertex(EndPoint, glm::vec3(0, 0, 1));
	QuadBuffer.AddVertex(EndPoint + glm::vec2(20, 0), glm::vec3(0, 0, 1));
	QuadBuffer.AddVertex(EndPoint + glm::vec2(20, 20), glm::vec3(0, 0, 1));
	QuadBuffer.AddVertex(EndPoint + glm::vec2(0, 20), glm::vec3(0, 0, 1));
	QuadBuffer.Dispose();
	
	MST.clear();
	Triangulation.Clear();
	Triangles.clear();
	Edges.clear();
	CentersPoints.clear();

	NodesCenters.clear();
	
	ClearTree(&Tree);
	
	//later create floor
	TileMap.CreateWalls();
	//fill buffer based on 
	FillTileBuffer();
	//dispose buffer
	if(!AtlasBuffer.Dispose()) return false;
	
	return true;
}

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

static void mousepos_callback(GLFWwindow* window, double x, double y) {
	MouseSceneCoord = Scene.WindowPosToWorldPos(x, y);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, GLFW_TRUE);
		return;
	}
	if(key == 'R' && action == GLFW_PRESS) {
		GenerateLevel();
	}
}

bool InitApp() {
	LogFile<<"Starting application"<<endl;    
    glfwSetErrorCallback(error_callback);
    
    if(!glfwInit()) return false;
    window = glfwCreateWindow(WindowWidth, WindowHeight, "TestApp", NULL, NULL);
    if(!window) {
        glfwTerminate();
        return false;
    }
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mousepos_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwMakeContextCurrent(window);
    if(glfwExtensionSupported("WGL_EXT_swap_control")) {
    	LogFile<<"Window: V-Sync supported. V-Sync: "<<EnableVsync<<endl;
		glfwSwapInterval(EnableVsync);//0 - disable, 1 - enable
	}
	else LogFile<<"Window: V-Sync not supported"<<endl;
    LogFile<<"Window created: width: "<<WindowWidth<<" height: "<<WindowHeight<<endl;

	//glew
	GLenum Error = glewInit();
	if(GLEW_OK != Error) {
		LogFile<<"Window: GLEW Loader error: "<<glewGetErrorString(Error)<<endl;
		return false;
	}
	LogFile<<"GLEW initialized"<<endl;
	
	if(!CheckOpenglSupport()) return false;

	//shaders
	if(!Shader.CreateShaderProgram("shaders/main.vertexshader.glsl", "shaders/main.fragmentshader.glsl")) return false;
	if(!Shader.AddUniform("MVP", "MVP")) return false;
	if(!Shader.AddUniform("TextureId", "myTextureSampler")) return false;
	if(!LineShader.CreateShaderProgram("shaders/lines.vertexshader.glsl", "shaders/lines.fragmentshader.glsl")) return false;
	if(!LineShader.AddUniform("MVP", "MVP")) return false;
	LogFile<<"Shaders loaded"<<endl;

	//scene
	if(!Scene.Initialize(&WindowWidth, &WindowHeight)) return false;
	LogFile<<"Scene initialized"<<endl;

	//randomize
    srand(time(NULL));
    LogFile<<"Randomized"<<endl;
    
    //other initializations
    //load textures
    txAtlas = TextureLoader.LoadTexture("textures/tex04.png", 1, 1, 0, txAtlas_cnt, GL_NEAREST, GL_REPEAT);
    if(!txAtlas) return false;
    //init buffers
    if(!LinesBuffer.Initialize()) return false;
	LinesBuffer.SetPrimitiveType(GL_LINES);
	if(!QuadBuffer.Initialize()) return false;
	QuadBuffer.SetPrimitiveType(GL_QUADS);
	if(!AtlasBuffer.Initialize(txAtlas, 32, 32, 2, 2)) return false;
    //init map
    TileMap = MTileMap(TilesCount[0], TilesCount[1]);
    //generate level
	if(!GenerateLevel()) return false;
	
	//turn off pause
	Pause = false;
    
    return true;
}

void RenderStep() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(Shader.ProgramId);
	glUniformMatrix4fv(Shader.Uniforms["MVP"], 1, GL_FALSE, Scene.GetDynamicMVP());
	glUniform1i(Shader.Uniforms["TextureId"], 0);
	
	//draw functions
	AtlasBuffer.Begin();
	AtlasBuffer.Draw();
	AtlasBuffer.End();
	
	glUseProgram(LineShader.ProgramId);
	glUniformMatrix4fv(LineShader.Uniforms["MVP"], 1, GL_FALSE, Scene.GetDynamicMVP());
	LinesBuffer.Begin();
	LinesBuffer.Draw();
	QuadBuffer.Draw();
	LinesBuffer.End();
}

void ClearApp() {
	//clear funstions
	QuadBuffer.Close();
	LinesBuffer.Close();
	AtlasBuffer.Close();
	TileMap.Close();
	TextureLoader.DeleteTexture(txAtlas, txAtlas_cnt);
	TextureLoader.Close();
	
	memset(keys, 0, 1024);
	Shader.Close();
	LineShader.Close();
	LogFile<<"Application: closed"<<endl;
}

int main(int argc, char** argv) {
	LogFile<<"Application: started"<<endl;
	if(!InitApp()) {
		ClearApp();
		glfwTerminate();
		LogFile.close();
		return 0;
	}
	FPSController.Initialize(glfwGetTime());
	while(!glfwWindowShouldClose(window)) {
		FPSController.FrameStep(glfwGetTime());
    	FPSController.FrameCheck();
		RenderStep();
        glfwSwapBuffers(window);
        glfwPollEvents();
	}
	ClearApp();
    glfwTerminate();
    LogFile.close();
}
