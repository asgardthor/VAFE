//-------------------------------------------------------------------------
// ----------   Xyce Includes in Cxx ----------
#include <Xyce_config.h>
#include "res_va.h"

#include <N_DEV_Const.h>
#include <N_DEV_DeviceOptions.h>
#include <N_DEV_DeviceMaster.h>
#include <N_DEV_ExternData.h>
#include <N_DEV_SolverState.h>
#include <N_DEV_Message.h>

#include <N_LAS_Matrix.h>
#include <N_LAS_Vector.h>

  #include <N_ANP_NoiseData.h>
#include <N_UTL_FeatureTest.h>
#if defined(HAVE_UNORDERED_MAP)
#include <unordered_map>
using std::unordered_map;
#elif defined(HAVE_TR1_UNORDERED_MAP)
#include <tr1/unordered_map>
using std::tr1::unordered_map;
#else
#error neither unordered_map or tr1/unordered_map found!
#endif

//The C++ Main part of Model Implementation Starts here
namespace Xyce {
namespace Device {
namespace COGENDAres_va {
JacobianStamp Instance::jacStamp;
IdVector Instance::nodeMap;
PairMap Instance::pairToJacStampMap;

    const int Instance::cogendaNodeID_p;
    const int Instance::cogendaNodeID_n;
    const int Instance::cogendaNodeID_GND;

/* class Traits member functions */
  //void Traits::loadInstanceParameters() 
void Traits::loadInstanceParameters(ParametricData<COGENDAres_va::Instance> &p)
{
  // This kludge is to force us always to have an instance parameter
  // that the device manager can set to the temperature, even if we have
  // no "TEMP".
  p.addPar("XYCEADMSINSTTEMP", 0.0, &COGENDAres_va::Instance::cogendaInstTemp)
    .setExpressionAccess(NO_DOC)
    .setUnit(U_DEGK)
    .setCategory(CAT_TEMP)
    .setDescription("Internally set parameter for device instance temperature");
  p.addPar("DW",static_cast<double>(1), &COGENDAres_va::Instance::dw);
  p.addPar("L",static_cast<double>(1), &COGENDAres_va::Instance::l);
  p.addPar("REND",static_cast<double>(1), &COGENDAres_va::Instance::rend);
  p.addPar("RES_TYPE",static_cast<double>(1), &COGENDAres_va::Instance::res_type);
  p.addPar("RS",static_cast<double>(1), &COGENDAres_va::Instance::rs);
  p.addPar("TC1",static_cast<double>(1), &COGENDAres_va::Instance::tc1);
  p.addPar("TC2",static_cast<double>(1), &COGENDAres_va::Instance::tc2);
  p.addPar("TEMPER",static_cast<double>(25), &COGENDAres_va::Instance::temper);
  p.addPar("VCR1",static_cast<double>(1), &COGENDAres_va::Instance::vcr1);
  p.addPar("VCR2",static_cast<double>(1), &COGENDAres_va::Instance::vcr2);
  p.addPar("W",static_cast<double>(1), &COGENDAres_va::Instance::w);
}

  //void Traits::loadModelParameters()
void Traits::loadModelParameters(ParametricData<COGENDAres_va::Model> &p)
{
  p.addPar("XYCEADMSMODTEMP", 0.0, &COGENDAres_va::Model::cogendaModTemp)
    .setExpressionAccess(NO_DOC)
    .setUnit(U_DEGK)
    .setCategory(CAT_TEMP)
    .setDescription("Internally set parameter for device model temperature");
  p.addPar("DW",static_cast<double>(1), &COGENDAres_va::Model::dw);
  p.addPar("L",static_cast<double>(1), &COGENDAres_va::Model::l);
  p.addPar("REND",static_cast<double>(1), &COGENDAres_va::Model::rend);
  p.addPar("RES_TYPE",static_cast<double>(1), &COGENDAres_va::Model::res_type);
  p.addPar("RS",static_cast<double>(1), &COGENDAres_va::Model::rs);
  p.addPar("TC1",static_cast<double>(1), &COGENDAres_va::Model::tc1);
  p.addPar("TC2",static_cast<double>(1), &COGENDAres_va::Model::tc2);
  p.addPar("TEMPER",static_cast<double>(25), &COGENDAres_va::Model::temper);
  p.addPar("VCR1",static_cast<double>(1), &COGENDAres_va::Model::vcr1);
  p.addPar("VCR2",static_cast<double>(1), &COGENDAres_va::Model::vcr2);
  p.addPar("W",static_cast<double>(1), &COGENDAres_va::Model::w);
}

/* class Instance member functions */
bool Instance::processParams () {
  if(!given("DW"))
    dw = model_.dw;
  if(!given("L"))
    l = model_.l;
  if(!given("REND"))
    rend = model_.rend;
  if(!given("RES_TYPE"))
    res_type = model_.res_type;
  if(!given("RS"))
    rs = model_.rs;
  if(!given("TC1"))
    tc1 = model_.tc1;
  if(!given("TC2"))
    tc2 = model_.tc2;
  if(!given("TEMPER"))
    temper = model_.temper;
  if(!given("VCR1"))
    vcr1 = model_.vcr1;
  if(!given("VCR2"))
    vcr2 = model_.vcr2;
  if(!given("W"))
    w = model_.w;
  updateTemperature(cogendaInstTemp);
  return true;
}

Instance::Instance(
  const Configuration & configuration,
  const InstanceBlock & instance_block,
  Model &               model,
  const FactoryBlock &  factory_block)
  : DeviceInstance(instance_block, configuration.getInstanceParameters(), factory_block),
    model_(model),
    li_p(-1),
    li_n(-1),
    li_branch_ip(-1),
    f_p_Equ_p_Node_Ptr(0),
    f_p_Equ_n_Node_Ptr(0),
    f_n_Equ_p_Node_Ptr(0),
    f_n_Equ_n_Node_Ptr(0),
    q_p_Equ_p_Node_Ptr(0),
    q_p_Equ_n_Node_Ptr(0),
    q_n_Equ_p_Node_Ptr(0),
    q_n_Equ_n_Node_Ptr(0),
    m_p_Equ_p_NodeOffset(-1),
    m_p_Equ_n_NodeOffset(-1),
    m_n_Equ_p_NodeOffset(-1),
    m_n_Equ_n_NodeOffset(-1),
      cogendaTemperature(getDeviceOptions().temp.getImmutableValue<double>())
{
  numExtVars = 2;
  numIntVars = 0;
  setNumStoreVars(0);
  setNumBranchDataVars(0);
  numBranchDataVarsIfAllocated = 2;
  leadCurrentF.resize(2);
  leadCurrentQ.resize(2);
  PairVector jacobianElements;
  jacobianElements.push_back(IntPair(cogendaNodeID_p,cogendaNodeID_p));
  jacobianElements.push_back(IntPair(cogendaNodeID_p,cogendaNodeID_n));
  jacobianElements.push_back(IntPair(cogendaNodeID_n,cogendaNodeID_p));
  jacobianElements.push_back(IntPair(cogendaNodeID_n,cogendaNodeID_n));

  setDefaultParams();
  setParams(instance_block.params);

  if (!given("XYCEADMSINSTTEMP"))
    cogendaInstTemp=getDeviceOptions().temp.getImmutableValue<double>();

  updateDependentParameters();

  processParams();
  initInternalVars();

  PairVector collapsedNodes;
  collapseNodes();

  if (jacStamp.empty())
  {
    int originalSize = 2;
    computeJacStampAndMaps(jacobianElements,collapsedNodes,jacStamp,nodeMap,pairToJacStampMap,originalSize);
  }
}

  void Instance::collapseNodes() {
}

  Instance::~Instance(){}

  void Instance::registerLIDs( const LocalIdVector & intLIDVecRef,
                               const LocalIdVector & extLIDVecRef)
{
  AssertLIDs(intLIDVecRef.size() == numIntVars);
  AssertLIDs(extLIDVecRef.size() == numExtVars);
  LocalIdVector localLIDVec;

  intLIDVec = intLIDVecRef;
  extLIDVec = extLIDVecRef;
  localLIDVec.resize(numExtVars+numIntVars);
  for (int localNodeIndex=0;localNodeIndex<numExtVars;++localNodeIndex)
  {
    localLIDVec[localNodeIndex]=extLIDVec[localNodeIndex];
  }
  for (int localNodeIndex=numExtVars;localNodeIndex<numExtVars+numIntVars;++localNodeIndex)
  {
    localLIDVec[localNodeIndex]=intLIDVec[localNodeIndex-numExtVars];
  }

  li_p = localLIDVec[nodeMap[cogendaNodeID_p]];
  li_n = localLIDVec[nodeMap[cogendaNodeID_n]];
}

  //void Instance::initInternalVars
void Instance::initInternalVars() {
}

  //void Instance::loadNodeSymbols
void Instance::loadNodeSymbols(Util::SymbolTable &symbol_table) const
{
  if (loadLeadCurrent)
  {
    addBranchDataNode( symbol_table, li_branch_ip, getName(), "BRANCH_D1");
  }
}

void Instance::registerStoreLIDs( const LocalIdVector & stoLIDVecRef)
{
  AssertLIDs(stoLIDVecRef.size() == getNumStoreVars());
}

void Instance::registerBranchDataLIDs(const std::vector<int> & branchLIDVecRef)
{
  AssertLIDs(branchLIDVecRef.size() == getNumBranchDataVars());
  if (loadLeadCurrent)
  {    
        int i = 0;
        li_branch_ip = branchLIDVecRef[i++];
  }
}

const JacobianStamp & Instance::jacobianStamp() const
{  return jacStamp; }

  //void Instance::registerJacLIDs
void Instance::registerJacLIDs( const JacobianStamp & jacLIDVec)
{
  DeviceInstance::registerJacLIDs(jacLIDVec);
  IntPair jacLoc;
  jacLoc = pairToJacStampMap[IntPair(cogendaNodeID_p,cogendaNodeID_p)];
  m_p_Equ_p_NodeOffset = jacLIDVec[jacLoc.first][jacLoc.second];
  jacLoc = pairToJacStampMap[IntPair(cogendaNodeID_p,cogendaNodeID_n)];
  m_p_Equ_n_NodeOffset = jacLIDVec[jacLoc.first][jacLoc.second];
  jacLoc = pairToJacStampMap[IntPair(cogendaNodeID_n,cogendaNodeID_p)];
  m_n_Equ_p_NodeOffset = jacLIDVec[jacLoc.first][jacLoc.second];
  jacLoc = pairToJacStampMap[IntPair(cogendaNodeID_n,cogendaNodeID_n)];
  m_n_Equ_n_NodeOffset = jacLIDVec[jacLoc.first][jacLoc.second];
}

  //void Instance::setupPointers
void Instance::setupPointers( )
{
  Linear::Matrix * dFdxMatPtr = extData.dFdxMatrixPtr;
  Linear::Matrix * dQdxMatPtr = extData.dQdxMatrixPtr;
  f_p_Equ_p_Node_Ptr = dFdxMatPtr->returnRawEntryPointer(li_p,li_p);
  f_p_Equ_n_Node_Ptr = dFdxMatPtr->returnRawEntryPointer(li_p,li_n);
  f_n_Equ_p_Node_Ptr = dFdxMatPtr->returnRawEntryPointer(li_n,li_p);
  f_n_Equ_n_Node_Ptr = dFdxMatPtr->returnRawEntryPointer(li_n,li_n);
  q_p_Equ_p_Node_Ptr = dQdxMatPtr->returnRawEntryPointer(li_p,li_p);
  q_p_Equ_n_Node_Ptr = dQdxMatPtr->returnRawEntryPointer(li_p,li_n);
  q_n_Equ_p_Node_Ptr = dQdxMatPtr->returnRawEntryPointer(li_n,li_p);
  q_n_Equ_n_Node_Ptr = dQdxMatPtr->returnRawEntryPointer(li_n,li_n);
}

  //bool Instance::loadDAEFVector
bool Instance::loadDAEFVector()
{
  bool bsuccess=true;
  (*extData.daeFVectorPtr)[li_p] += staticContributions[cogendaNodeID_p].val();
  (*extData.daeFVectorPtr)[li_n] += staticContributions[cogendaNodeID_n].val();
  if (loadLeadCurrent)
  {
    double * leadF = extData.nextLeadCurrFCompRawPtr;
          leadF[li_branch_ip] = leadCurrentF[cogendaNodeID_p];
  }
  return bsuccess;
}

  //bool Instance::loadDAEQVector
bool Instance::loadDAEQVector()
{
  (*extData.daeQVectorPtr)[li_p] += dynamicContributions[cogendaNodeID_p].val();
  (*extData.daeQVectorPtr)[li_n] += dynamicContributions[cogendaNodeID_n].val();
  if (loadLeadCurrent)
  {
    double * leadQ = extData.nextLeadCurrQCompRawPtr;
          leadQ[li_branch_ip] = leadCurrentQ[cogendaNodeID_p];
  }
  return true;
}

  //bool Instance::updatePrimaryState()
bool Instance::updatePrimaryState()
{
  bool bsuccess = true;
  initInternalVars();
  bsuccess = updateIntermediateVars();
  return bsuccess;
}

  //bool Instance::updateSecondaryState()
bool Instance::updateSecondaryState()
{
  bool bsuccess = true;
  return bsuccess;
}

  //bool Instance::updateIntermediateVars
bool Instance::updateIntermediateVars()
{
  bool bsuccess=true;
  Linear::Vector * solVectorPtr = extData.nextSolVectorPtr;
  // set the sizes of the Fad arrays:
  if (probeVars.size() != (1))
  {
    probeVars.resize(1);
    staticContributions.resize(2+0);
    dynamicContributions.resize(2+0);
  }

  for (int i=0; i < 2+0 ; ++i)
  {
     staticContributions[i]=0;
     dynamicContributions[i]=0;
  }

  probeVars[cogendaProbeID_V_p_n] = (*solVectorPtr)[li_p] - (*solVectorPtr)[li_n];
  probeVars[cogendaProbeID_V_p_n].diff(cogendaProbeID_V_p_n,1);

{
  tcoef = ((((1.0+(this->tc1*(this->temper-25)))+((this->tc2*(this->temper-25))*(this->temper-25))))*UNITFAD).val();
  if((this->res_type == 1)) {
    {
      res = (((((2*this->rend)/(this->w+this->dw))+((this->rs*this->l)/(this->w+this->dw)))*((1.0+(this->vcr1*fabs(probeVars[cogendaProbeID_V_p_n])))+(this->vcr2*pow(fabs(probeVars[cogendaProbeID_V_p_n]),2))))*tcoef);
    }
  }
  if((this->res_type == 2)) {
    {
      res = (((((2*this->rend)/(this->w+this->dw))+((this->rs*this->l)/(this->w+this->dw)))*((1.0+tanh(((((this->vcr1*fabs(probeVars[cogendaProbeID_V_p_n]))/this->l)+(this->vcr2*pow((fabs(probeVars[cogendaProbeID_V_p_n])/this->l),2)))-0.1)))-tanh(-0.1)))*tcoef);
    }
  }
//I-contrib...
  staticContributions[cogendaNodeID_p] += (probeVars[cogendaProbeID_V_p_n]/res);
  staticContributions[cogendaNodeID_n] -= (probeVars[cogendaProbeID_V_p_n]/res);
  v = probeVars[cogendaProbeID_V_p_n];
  i = (v/res);
  power = (i*v);
}
  if (loadLeadCurrent) {
    for ( int unCtNode=0; unCtNode < 2 ; unCtNode++) {
      leadCurrentF[unCtNode] = 0.0;
      leadCurrentQ[unCtNode] = 0.0;
    }
    for ( int unCtNode=0; unCtNode < 2; unCtNode++) {
      if (nodeMap[unCtNode] < 2 && nodeMap[unCtNode] != -1 ) {
        leadCurrentF[nodeMap[unCtNode]] += staticContributions[unCtNode].val();
        leadCurrentQ[nodeMap[unCtNode]] += dynamicContributions[unCtNode].val();
      }
    }
  }
  return true;
}

  //bool Instance::loadDAEdFdx //Load the dFdx static jacobian matrix
bool Instance::loadDAEdFdx()
{
  bool bsuccess = true;
  Linear::Matrix & dFdx = *(extData.dFdxMatrixPtr);
#ifndef Xyce_NONPOINTER_MATRIX_LOAD
  (*f_n_Equ_n_Node_Ptr) +=  -staticContributions[cogendaNodeID_n].dx(cogendaProbeID_V_p_n);
  (*f_n_Equ_p_Node_Ptr) +=  +staticContributions[cogendaNodeID_n].dx(cogendaProbeID_V_p_n);
  (*f_p_Equ_n_Node_Ptr) +=  -staticContributions[cogendaNodeID_p].dx(cogendaProbeID_V_p_n);
  (*f_p_Equ_p_Node_Ptr) +=  +staticContributions[cogendaNodeID_p].dx(cogendaProbeID_V_p_n);
#else
  //use the offsets instead of pointers
  dFdx[li_n][m_n_Equ_n_NodeOffset] +=  -staticContributions[cogendaNodeID_n].dx(cogendaProbeID_V_p_n);
  dFdx[li_n][m_n_Equ_p_NodeOffset] +=  +staticContributions[cogendaNodeID_n].dx(cogendaProbeID_V_p_n);
  dFdx[li_p][m_p_Equ_n_NodeOffset] +=  -staticContributions[cogendaNodeID_p].dx(cogendaProbeID_V_p_n);
  dFdx[li_p][m_p_Equ_p_NodeOffset] +=  +staticContributions[cogendaNodeID_p].dx(cogendaProbeID_V_p_n);
#endif
  return bsuccess;
}

  //bool Instance::loadDAEdQdx
bool Instance::loadDAEdQdx()
{
  bool bsuccess = true;
  Linear::Matrix & dQdx = *(extData.dQdxMatrixPtr);
#ifndef Xyce_NONPOINTER_MATRIX_LOAD
#else
  //use the offsets instead of pointers
#endif
  return bsuccess;
}

bool Instance::updateTemperature(const double & temperatureTemp)
{
  cogendaTemperature = temperatureTemp;
  cogenda_vt_nom = _VT0_(temperatureTemp);
  return true;
}
/* class Model member functions */
  //bool Model::processParams
  bool Model::processParams()
  {
  if(!given("DW"))
    dw = 1;
  if(!given("L"))
    l = 1;
  if(!given("REND"))
    rend = 1;
  if(!given("RES_TYPE"))
    res_type = 1;
  if(!given("RS"))
    rs = 1;
  if(!given("TC1"))
    tc1 = 1;
  if(!given("TC2"))
    tc2 = 1;
  if(!given("TEMPER"))
    temper = 25;
  if(!given("VCR1"))
    vcr1 = 1;
  if(!given("VCR2"))
    vcr2 = 1;
  if(!given("W"))
    w = 1;
}

  //bool Model::processInstanceParams
bool Model::processInstanceParams()
{
  std::vector<Instance*>::iterator iter;
  std::vector<Instance*>::iterator first = instanceContainer.begin();
  std::vector<Instance*>::iterator last  = instanceContainer.end();
  for (iter=first; iter!=last; ++iter)
  {
    (*iter)->processParams();
  }
  return true;
}

  //Model::Model(..){...}
Model::Model(
  const Configuration & configuration,
  const ModelBlock &    model_block,
  const FactoryBlock &  factory_block)
  : DeviceModel(model_block, configuration.getModelParameters(), factory_block),
    dw(1),
    l(1),
    rend(1),
    res_type(1),
    rs(1),
    tc1(1),
    tc2(1),
    temper(25),
    vcr1(1),
    vcr2(1),
    w(1)
{
  setDefaultParams();
  setModParams(model_block.params);
  if (!given("XYCEADMSMODTEMP"))
    cogendaModTemp=getDeviceOptions().temp.getImmutableValue<double>();
  updateDependentParameters();
  processParams();
}

  //Model::~Model
Model::~Model()
{
  std::vector<Instance*>::iterator iterI;
  std::vector<Instance*>::iterator firstI = instanceContainer.begin ();
  std::vector<Instance*>::iterator lastI  = instanceContainer.end ();
  for (iterI = firstI; iterI != lastI; ++iterI)
    delete (*iterI);
}

  //std::ostream &Model::printOutInstances
std::ostream &Model::printOutInstances(std::ostream &os) const
{
  std::vector<Instance*>::const_iterator iter;
  std::vector<Instance*>::const_iterator first = instanceContainer.begin();
  std::vector<Instance*>::const_iterator last  = instanceContainer.end();
  int i;
  os << std::endl;
  os << "  name   model name  Parameters" << std::endl;
  for (i=0, iter=first; iter!=last; ++iter, ++i)
  {
    os << "  " << i << ": " << (*iter)->getName() << "      ";
    os << getName();
    os << std::endl;
    os << std::endl;
  }
  os << std::endl;
  return os;
}

  //void Model::forEachInstance()
void Model::forEachInstance(DeviceInstanceOp &op) const 
{
  for (std::vector<Instance *>::const_iterator it = instanceContainer.begin(); it != instanceContainer.end(); ++it)
    op(*it);
}

/* class Traits member functions */
  //Device *Traits::factory()
Device *Traits::factory(const Configuration &configuration, const FactoryBlock &factory_block)
  {
    return new DeviceMaster<Traits>(configuration, factory_block, factory_block.solverState_, factory_block.deviceOptions_);
}
void registerDevice()
{
  Config<Traits>::addConfiguration()
    .registerDevice("res_va", 1)
    .registerModelType("res_va", 1);
}
} // namespace COGENDAres_va
} // namespace Device
} // namespace Xyce

struct Bootstrap {
 Bootstrap() {
  Xyce::Device::COGENDAres_va::registerDevice(); }}; 
Bootstrap COGENDAres_va_bootstrap;

