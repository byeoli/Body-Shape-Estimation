// GK-Undressing-People-Ceres.cpp : This file contains the 'main' function. Program execution begins and ends there.
// It shows the example of how to use the code developed for the Undressing the input scan
//

//#define DEBUG
//#define EIGEN_STACK_ALLOCATION_LIMIT 0

// need to include first, because it uses Windows.h
#include "PoseShapeExtractor.h"
#include <GeneralMesh/GeneralMesh.h>
#include "SMPLWrapper.h"

/*
    TODO initial pose estimation with openpose
    TODO use normalized General Mesh vertices for optimization 
    TODO Shape regularization 
    TODO directional pose estimation -- idea: add it to the main objective as additional resudual
    TODO move (important) parameters outside
    TODO Idea: allow start optimization from the last results
    TODO libigl as static library
    TODO SMPL wrapper avalible for everyone
*/


static constexpr char output_path[] = "C:/Users/Maria/MyDocs/GigaKorea/GK-Undressing-People-Ceres/Outputs/";
static constexpr char smpl_model_path[] = "C:/Users/Maria/MyDocs/GigaKorea/GK-Undressing-People-Ceres/Resources";

void generateSMPLoutput()
{
    SMPLWrapper new_smpl('f', smpl_model_path);
    SMPLWrapper::State smpl_state_ptrs = new_smpl.getStatePointers();

    smpl_state_ptrs.pose[50] = -0.7854; // pi/4
    smpl_state_ptrs.pose[53] = 0.7854;
    //smpl_state_ptrs.shape[0] = -0.5;

    CustomLogger logger(output_path, "smpl_output");

    logger.saveFinalModel(new_smpl);
}

int main()
{
    try {
        char gender = 'f';
        //const char* input_name = "D:/Data/DYNA/50004_jumping_jacks/00000.obj";  // A-pose
        //const char* input_name = "D:/Data/DYNA/50004_punching/00053.obj";  // A-pose
        const char* input_name = "D:/Data/SketchFab/Sexy Girl.obj"; 
        //const char* input_name = "D:/Data/SketchFab/shan.obj"; 
        //gender = 'm';
        //const char* input_name = "D:/Data/SketchFab/Web.obj";
        //const char* input_name = "D:/Data/SketchFab/casual-man.obj";

        // for SMPL/DYNA inputs
        //const char* input_key_vertices_name = "D:/Data/smpl_outs/smpl_key_vertices.txt";
        //input = new GeneralMesh(input_name, input_key_vertices_name);

        GeneralMesh input(input_name);
        std::cout << "Input mesh loaded!" << std::endl;

        PoseShapeExtractor extractor(smpl_model_path, gender,
            "C:/Users/Maria/MyDocs/libs/Installed_libs/ml_models/openpose",
            "C:/Users/Maria/MyDocs/GigaKorea/GK-Undressing-People-Ceres/Resources",
            output_path);
        
        extractor.setupNewExperiment(&input, "main_refactor");

        SMPLWrapper* smpl = extractor.runExtraction();

        extractor.viewFinalResult(true);

    }
    catch (std::exception& e)
    {
        std::cout << "Exception encountered: " << e.what() << std::endl
            << "Terminating." << std::endl;
    }
}

