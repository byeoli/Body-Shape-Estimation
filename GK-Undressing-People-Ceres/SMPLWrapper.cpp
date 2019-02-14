#include "pch.h"
#include "SMPLWrapper.h"


SMPLWrapper::SMPLWrapper(char gender, const char* path)
{
#ifdef USE_CERES
    std::cerr << "Warning: Use of Ceres for SMPL posing is enabled." << std::endl;
#endif // USE_CERES

    // set the info
    if (gender != 'f' && gender != 'm') 
    {
        std::string message("Wrong gender supplied: ");
        message += gender;
        throw std::exception(message.c_str());
    }
    this->gender_ = gender;

    // !!!! expects a pre-defined file structure
    // TODO remove specific structure expectation
    this->general_path_ = path;
    this->general_path_ += '/';

    this->gender_path_ = path;
    this->gender_path_ += '/';
    this->gender_path_ += gender;
    this->gender_path_ += "_smpl/";

    this->readTemplate_();
    this->readJointMat_();
    this->readShapes_();
    this->readWeights_();
    this->readHierarchy_();

    this->template_mean_point_ = this->verts_template_.colwise().mean();

}


SMPLWrapper::~SMPLWrapper()
{
}


E::MatrixXd SMPLWrapper::calcModel(const double * const pose, const double * const shape, E::MatrixXd * pose_jac, E::MatrixXd * shape_jac) const
{
    // assignment won't work without cast
    E::MatrixXd verts = this->verts_template_;
#ifdef DEBUG
    std::cout << "Calc model" << std::endl;
#endif // DEBUG

    if (shape != nullptr)
    {
        this->shapeSMPL_(shape, verts, shape_jac);
    }

    if (pose != nullptr)
    {
        this->poseSMPL_(pose, verts, pose_jac);

        if (shape_jac != nullptr)
        {
            // WARNING! This will significantly increse iteration time
            for (int i = 0; i < SMPLWrapper::SHAPE_SIZE; ++i)
            {
                // TODO: add the use of pre-computed LBS Matrices 
                // TODO: test
                this->poseSMPL_(pose, shape_jac[i]);
            }
        }
#ifdef DEBUG
        std::cout << "Fin posing " << verts.rows() << " x " << verts.cols() << std::endl;
#endif // DEBUG
    }

#ifdef DEBUG
    std::cout << "Fin calculating" << std::endl;
    //if (std::is_same_v<T, double>)
    //{
    //    for (int i = 0; i < SMPLWrapper::VERTICES_NUM; i++)
    //    {
    //        for (int j = 0; j < SMPLWrapper::SPACE_DIM; j++)
    //        {
    //            std::cout << verts(i, j) << " ";
    //        }
    //        std::cout << std::endl;
    //    }
    //}
#endif // DEBUG

    return verts;
}


E::MatrixXd SMPLWrapper::calcJointLocations(const double * shape)
{
    E::MatrixXd verts = this->calcModelTemplate<double>(nullptr, shape);
    
    return this->jointRegressorMat_ * verts;
}


void SMPLWrapper::saveToObj(const double* translation, const double* pose, const double* shape, const std::string path) const
{
    MatrixXt<double> verts = this->calcModelTemplate<double>(pose, shape);
    
    if (translation != nullptr)
    {
        for (int i = 0; i < SMPLWrapper::VERTICES_NUM; i++)
        {
            for (int j = 0; j < SMPLWrapper::SPACE_DIM; ++j)
            {
                verts(i, j) += translation[j];
            }
        }
    }

    igl::writeOBJ(path, verts, this->faces_);
}


void SMPLWrapper::readTemplate_()
{
    std::string file_name(this->gender_path_);
    file_name += this->gender_;
    file_name += "_shapeAv.obj";

    bool success = igl::readOBJ(file_name, this->verts_template_, this->faces_);
    if (!success)
    {
        std::string message("Abort: Could not read SMPL template at ");
        message += file_name;
        throw std::exception(message.c_str());
    }
}


void SMPLWrapper::readJointMat_()
{
    std::string file_name(this->gender_path_);
    file_name += this->gender_;
    file_name += "_joints_mat.txt";

    // copy from Meekyong code example
    std::fstream inFile;
    inFile.open(file_name, std::ios_base::in);
    int joints_n, verts_n;
    inFile >> joints_n;
    inFile >> verts_n;
    // Sanity check
    if (joints_n != SMPLWrapper::JOINTS_NUM || verts_n != SMPLWrapper::VERTICES_NUM)
        throw std::exception("Joint matrix info (number of joints and vertices) is incompatible with the model");

    this->jointRegressorMat_.resize(joints_n, verts_n);
    for (int i = 0; i < joints_n; i++)
        for (int j = 0; j < verts_n; j++)
            inFile >> this->jointRegressorMat_(i, j);

    inFile.close();
}


void SMPLWrapper::readShapes_()
{
    std::string file_path(this->gender_path_);
    file_path += this->gender_;
    file_path += "_blendshape/shape";

    Eigen::MatrixXi fakeFaces(SMPLWrapper::VERTICES_NUM, SMPLWrapper::SPACE_DIM);

    for (int i = 0; i < SMPLWrapper::SHAPE_SIZE; i++)
    {
        std::string file_name(file_path);
        file_name += std::to_string(i);
        file_name += ".obj";

        igl::readOBJ(file_name, this->shape_diffs_[i], fakeFaces);

        this->shape_diffs_[i] -= this->verts_template_;
    }
}


void SMPLWrapper::readWeights_()
{
    std::string file_name(this->gender_path_);
    file_name += this->gender_;
    file_name += "_weight.txt";

    std::fstream inFile;
    inFile.open(file_name, std::ios_base::in);
    int joints_n, verts_n;
    inFile >> joints_n;
    inFile >> verts_n;
    // Sanity check
    if (joints_n != SMPLWrapper::JOINTS_NUM || verts_n != SMPLWrapper::VERTICES_NUM)
        throw std::exception("Weights info (number of joints and vertices) is incompatible with the model");

    std::vector<E::Triplet<double>> tripletList;
    tripletList.reserve(verts_n * SMPLWrapper::WEIGHTS_BY_VERTEX);     // for faster filling performance
    double tmp;
    for (int i = 0; i < verts_n; i++)
    {
        for (int j = 0; j < joints_n; j++)
        {
            inFile >> tmp;
            if (tmp > 0.00001)  // non-zero weight
                tripletList.push_back(E::Triplet<double>(i, j, tmp));
        }
    }
    this->weights_.resize(verts_n, joints_n);
    this->weights_.setFromTriplets(tripletList.begin(), tripletList.end());

#ifdef DEBUG
    std::cout << "Weight sizes " << this->weights_.outerSize() << " " << this->weights_.innerSize() << std::endl;
#endif // DEBUG

    inFile.close();
}


void SMPLWrapper::readHierarchy_()
{
    std::string file_name(this->general_path_);
    file_name += "jointsHierarchy.txt";

    std::fstream inFile;
    inFile.open(file_name, std::ios_base::in);
    int joints_n;
    inFile >> joints_n;
    // Sanity check
    if (joints_n != SMPLWrapper::JOINTS_NUM)
    {
        throw std::exception("Number of joints in joints hierarchy info is incompatible with the model");
    }
    
    int tmpId;
    for (int j = 0; j < joints_n; j++)
    {
        inFile >> tmpId;
        inFile >> this->joints_parents_[tmpId];
    }

    inFile.close();
}


void SMPLWrapper::shapeSMPL_(const double * const shape, E::MatrixXd &verts, E::MatrixXd* shape_jac) const
{
    for (int i = 0; i < this->SHAPE_SIZE; i++)
    {
        verts += shape[i] * this->shape_diffs_[i];
    }

    if (shape_jac != nullptr)
    {
        for (int i = 0; i < this->SHAPE_SIZE; i++)
        {
            shape_jac[i] = this->shape_diffs_[i];
        }
    }
}


void SMPLWrapper::poseSMPL_(const double * const pose, E::MatrixXd & verts, E::MatrixXd* pose_jac) const
{
#ifdef DEBUG
    std::cout << "pose (analytic)" << std::endl;
#endif // DEBUG

    E::MatrixXd jointLocations = this->jointRegressorMat_ * verts;
    E::MatrixXd jointsTransformation;

    jointsTransformation = this->getJointsTransposedGlobalTransformation_(pose, jointLocations, pose_jac);

    // TODO Use sparce matrices for LBS
    E::MatrixXd LBSMat = this->getLBSMatrix_(verts);

    verts = LBSMat * jointsTransformation;

    if (pose_jac != nullptr)
    {
        // pose_jac[i] at this point has the same structure as jointsTransformation
        for (int i = 0; i < SMPLWrapper::POSE_SIZE; ++i)
            pose_jac[i].applyOnTheLeft(LBSMat);
    }
}


E::MatrixXd SMPLWrapper::getJointsTransposedGlobalTransformation_(const double * const pose, E::MatrixXd & jointLocations, E::MatrixXd* jacsTotal) const
{
#ifdef DEBUG
    std::cout << "global transform (analytic)" << std::endl;
#endif // DEBUG

    // uses functions that assume input in 3D (see below)
    assert(SMPLWrapper::SPACE_DIM == 3 && "The function can only be used in 3D world");

    static constexpr int HOMO_SIZE = SMPLWrapper::SPACE_DIM + 1;
    E::Matrix<double, HOMO_SIZE, HOMO_SIZE> jointGlobalMats[SMPLWrapper::JOINTS_NUM];
    E::MatrixXd jointJac[SMPLWrapper::JOINTS_NUM][SMPLWrapper::POSE_SIZE];      // for jacobian calculations, 4x4 or empty
    // Stacked (transposed) global transformation matrices for points
    E::MatrixXd pointTransformTotal(HOMO_SIZE * SMPLWrapper::JOINTS_NUM, SMPLWrapper::SPACE_DIM);

    if (jacsTotal != nullptr)
    {
        // init the jac
        for (int i = 0; i < SMPLWrapper::POSE_SIZE; ++i)
        {
            jacsTotal[i].setZero(HOMO_SIZE * SMPLWrapper::JOINTS_NUM, SMPLWrapper::SPACE_DIM);
        }

        jointGlobalMats[0] = this->get3DLocalTransformMat_(pose, jointLocations.row(0), jointJac[0]);

        // fill derivatives w.r.t. 0 coordinates
        E::MatrixXd tmpPointGlobalJac;
        for (int i = 0; i < SMPLWrapper::SPACE_DIM; ++i)
        {
            tmpPointGlobalJac = jointJac[0][i] * this->get3DTranslationMat_(-jointLocations.row(0));
            jacsTotal[i].block(0, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM) = tmpPointGlobalJac.transpose().leftCols(SMPLWrapper::SPACE_DIM);

            std::cout << "Root Jacobian " << std::endl << jointJac[0][i] << std::endl;
            std::cout << "Root Global Jacobian " << i << std::endl << tmpPointGlobalJac << std::endl;
        }
    }
    else
    {
        jointGlobalMats[0] = this->get3DLocalTransformMat_(pose, jointLocations.row(0));
    }
    E::MatrixXd tmpPointGlobalTransform = jointGlobalMats[0] * this->get3DTranslationMat_(-jointLocations.row(0));
    pointTransformTotal.block(0, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM)
        = tmpPointGlobalTransform.transpose().leftCols(SMPLWrapper::SPACE_DIM);

    for (int i = 1; i < SMPLWrapper::JOINTS_NUM; i++)
    {
        if (jacsTotal != nullptr)
        {
            E::MatrixXd localTransform;
            E::MatrixXd localTransformJac[SMPLWrapper::SPACE_DIM];

            localTransform = this->get3DLocalTransformMat_((pose + i * 3),
                jointLocations.row(i) - jointLocations.row(this->joints_parents_[i]),
                localTransformJac);

            // Forward Kinematics Formula
            jointGlobalMats[i] = jointGlobalMats[this->joints_parents_[i]] * localTransform;
            
            E::MatrixXd tmpPointGlobalJac;
            for (int j = 0; j < SMPLWrapper::SPACE_DIM; ++j)
            {
                // for simplicity of application to the next generation, multiply by parent transfromation here
                jointJac[i][i * SMPLWrapper::SPACE_DIM + j] =
                    jointGlobalMats[this->joints_parents_[i]] * localTransformJac[j];
                // w.r.t. joint angles of the current joint
                tmpPointGlobalJac = jointJac[i][i * SMPLWrapper::SPACE_DIM + j] * this->get3DTranslationMat_(-jointLocations.row(i));
                jacsTotal[i * SMPLWrapper::SPACE_DIM + j].block(i * HOMO_SIZE, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM) - 
                    tmpPointGlobalJac.transpose().leftCols(SMPLWrapper::SPACE_DIM);
            }
            
            //jacsTotal[i * SMPLWrapper::SPACE_DIM].block(i * HOMO_SIZE, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM) 
            //    = jointJac[i][i * SMPLWrapper::SPACE_DIM].transpose().leftCols(SMPLWrapper::SPACE_DIM);
            //jacsTotal[i * SMPLWrapper::SPACE_DIM + 1].block(i * HOMO_SIZE, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM) 
            //    = jointJac[i][i * SMPLWrapper::SPACE_DIM + 1].transpose().leftCols(SMPLWrapper::SPACE_DIM);
            //jacsTotal[i * SMPLWrapper::SPACE_DIM + 2].block(i * HOMO_SIZE, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM)
            //    = jointJac[i][i * SMPLWrapper::SPACE_DIM + 2].transpose().leftCols(SMPLWrapper::SPACE_DIM);

            // jac w.r.t. parent joints
            // Using that id of the parent is always smaller that the child's id 
            //std::cout << "Current local jacobian size " << jointJac[i][i * SMPLWrapper::SPACE_DIM].size();
            //std::cout << 
            
            for (int j = 0; j < (this->joints_parents_[i] + 1) * SMPLWrapper::SPACE_DIM; ++j)
            {
                if (jointJac[this->joints_parents_[i]][j].size() > 0)
                {
                    //std::cout << "jac of joint " << i << "w.r.t. parent motion " << j / SMPLWrapper::SPACE_DIM << std::endl;
                    // TODO Recheck the need for inverse
                    jointJac[i][j] = jointJac[this->joints_parents_[i]][j] * localTransform; //* ; // 

                    tmpPointGlobalJac = jointJac[i][j] * this->get3DTranslationMat_(-jointLocations.row(i));

                    jacsTotal[j].block(i * HOMO_SIZE, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM)
                        = tmpPointGlobalJac.transpose().leftCols(SMPLWrapper::SPACE_DIM);
                }
            }
        }
        else // No jac calculations
        {
            // Forward Kinematics Formula
            jointGlobalMats[i] = jointGlobalMats[this->joints_parents_[i]]
                * this->get3DLocalTransformMat_((pose + i * 3), 
                    jointLocations.row(i) - jointLocations.row(this->joints_parents_[i]));
        }
        
        // collect transform for final matrix
        tmpPointGlobalTransform = jointGlobalMats[i] * this->get3DTranslationMat_(-jointLocations.row(i));

        pointTransformTotal.block(i * HOMO_SIZE, 0, HOMO_SIZE, SMPLWrapper::SPACE_DIM)
            = tmpPointGlobalTransform.transpose().leftCols(SMPLWrapper::SPACE_DIM);
    }

    return pointTransformTotal;
}


E::MatrixXd SMPLWrapper::get3DLocalTransformMat_(const double * const jointAxisAngleRotation, const E::MatrixXd & jointLocation, E::MatrixXd* localTransfromJac) const
{
    E::MatrixXd localTransform;
    localTransform.setIdentity(4, 4);   // in homogenious coordinates
    localTransform.block(0, 3, 3, 1) = jointLocation.transpose();

    if (localTransfromJac != nullptr)
    {
        for (int i = 0; i < 3; ++i)
        {
            localTransfromJac[i].setZero(4, 4);
            localTransfromJac[i](3, 3) = 1.;    // homogenious coordinates
            localTransfromJac[i].block(0, 3, 3, 1) = jointLocation.transpose();   // For the default pose transformation
        }
    }

#undef USE_CERES
#ifdef USE_CERES

    double* rotationMat = new double[9];
    ceres::AngleAxisToRotationMatrix(jointAxisAngleRotation, rotationMat);
    localTransform.block(0, 0, 3, 3) = Eigen::Map<E::MatrixXd>(rotationMat, 3, 3);

    delete[] rotationMat;

#else // USE_CERES

    double norm = sqrt(jointAxisAngleRotation[0] * jointAxisAngleRotation[0]
        + jointAxisAngleRotation[1] * jointAxisAngleRotation[1]
        + jointAxisAngleRotation[2] * jointAxisAngleRotation[2]);
    if (norm > 0.0001)  // don't waste computations on zero joint movement
    {
        // apply Rodrigues formula
        E::MatrixXd skew (3, 3);
        skew <<
            0, -jointAxisAngleRotation[2], jointAxisAngleRotation[1],
            jointAxisAngleRotation[2], 0, -jointAxisAngleRotation[0],
            -jointAxisAngleRotation[1], jointAxisAngleRotation[0], 0;

        skew /= norm;

        E::MatrixXd exponent;
        exponent.setIdentity(3, 3);
        exponent += skew * sin(norm) + skew * skew * (1. - cos(norm));
        localTransform.block(0, 0, 3, 3) = exponent;

        if (localTransfromJac != nullptr)
        {
            // TODO Make a class member for easier initialization
            E::MatrixXd skewDeriv[3];
            skewDeriv[0].resize(3, 3);
            skewDeriv[0] <<
                0, 0, 0,
                0, 0, -1,
                0, 1, 0;
            skewDeriv[1].resize(3, 3);
            skewDeriv[1] <<
                0, 0, 1,
                0, 0, 0,
                -1, 0, 0;
            skewDeriv[2].resize(3, 3);
            skewDeriv[2] <<
                0, -1, 0,
                1, 0, 0,
                0, 0, 0;

            E::MatrixXd skew2Deriv[3];
            skew2Deriv[0].resize(3, 3);
            skew2Deriv[0] <<
                0, jointAxisAngleRotation[1], jointAxisAngleRotation[2],
                jointAxisAngleRotation[1], -2 * jointAxisAngleRotation[0], 0,
                jointAxisAngleRotation[2], 0, -2 * jointAxisAngleRotation[0];
            skew2Deriv[1].resize(3, 3);
            skew2Deriv[1] <<
                -2 * jointAxisAngleRotation[1], jointAxisAngleRotation[0], 0,
                jointAxisAngleRotation[0], 0, jointAxisAngleRotation[2],
                0, jointAxisAngleRotation[2], -2 * jointAxisAngleRotation[1];
            skew2Deriv[2].resize(3, 3);
            skew2Deriv[2] <<
                -2 * jointAxisAngleRotation[2], 0, jointAxisAngleRotation[0],
                0, -2 * jointAxisAngleRotation[2], jointAxisAngleRotation[1],
                jointAxisAngleRotation[0], jointAxisAngleRotation[1], 0;

            for (int i = 0; i < 3; ++i)
            {
                // Derivation is in the notebook, double check with https://math.stackexchange.com/questions/2276003/derivative-of-rotation-matrix
                localTransfromJac[i].block(0, 0, 3, 3) =
                    skew * (cos(norm) * jointAxisAngleRotation[i] / norm)
                    + sin(norm) / norm * (skewDeriv[i] - skew * jointAxisAngleRotation[i] / norm)
                    + skew * skew * (sin(norm) * jointAxisAngleRotation[i] / norm)
                    + (skew2Deriv[i] - skew * skew * 2 * jointAxisAngleRotation[i]) * (1. - cos(norm)) / (norm * norm);
                // + sin(norm) * (- (skew * norm) * jointAxisAngleRotation[i] / (norm * norm * norm) + skewDeriv[i] / norm)
            }  
        }

    }
#endif // USE_CERES
#define USE_CERES

    return localTransform;
}

E::MatrixXd SMPLWrapper::get3DTranslationMat_(const E::MatrixXd & translationVector) const
{
    E::MatrixXd translation;
    translation.setIdentity(4, 4);  // in homogenious coordinates
    translation.block(0, 3, 3, 1) = translationVector.transpose();

    return translation;
}

E::SparseMatrix<double> SMPLWrapper::getLBSMatrix_(E::MatrixXd & verts) const
{
    const int dim = SMPLWrapper::SPACE_DIM;
    const int nVerts = SMPLWrapper::VERTICES_NUM;
    const int nJoints = SMPLWrapper::JOINTS_NUM;  // Number of joints
#ifdef DEBUG
    std::cout << "LBSMat: start (analytic)" << std::endl;
#endif // DEBUG
    // +1 goes for homogenious coordinates
    E::SparseMatrix<double> LBSMat(nVerts, (dim + 1) * nJoints);
    std::vector<E::Triplet<double>> LBSTripletList;
    LBSTripletList.reserve(nVerts * (dim + 1) * SMPLWrapper::WEIGHTS_BY_VERTEX);     // for faster filling performance

    // go over non-zero weight elements
    for (int k = 0; k < this->weights_.outerSize(); ++k)
    {
        for (E::SparseMatrix<double>::InnerIterator it(this->weights_, k); it; ++it)
        {
            double weight = it.value();
            int idx_vert = it.row();
            int idx_joint = it.col();
            // premultiply weigths by vertex homogenious coordinates
            for (int idx_dim = 0; idx_dim < dim; idx_dim++)
                LBSTripletList.push_back(E::Triplet<double>(idx_vert, idx_joint * (dim + 1) + idx_dim, weight * verts(idx_vert, idx_dim)));
            LBSTripletList.push_back(E::Triplet<double>(idx_vert, idx_joint * (dim + 1) + dim, weight));
        }
    }
    LBSMat.setFromTriplets(LBSTripletList.begin(), LBSTripletList.end());

    return LBSMat;
}

