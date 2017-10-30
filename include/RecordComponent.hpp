#pragma once


#include "Attributable.hpp"
#include "Dataset.hpp"
#include "Writable.hpp"


class RecordComponent : public Attributable
{
    template<
            typename T,
            typename T_key
    >
    friend class Container;
    friend class Iteration;
    friend class ParticleSpecies;
    template< typename T_elem >
    friend class BaseRecord;
    friend class Record;
    friend class Mesh;

protected:
    RecordComponent();

    void readBase();

    Dataset m_dataset;
    std::queue< IOTask > m_chunks;
    bool m_isConstant;
    Attribute m_constantValue;

public:
    double unitSI() const;
    RecordComponent& setUnitSI(double);

    RecordComponent& resetDataset(Dataset);

    Datatype getDatatype();
    uint8_t getDimensionality();
    Extent getExtent();

    template< typename T >
    RecordComponent& makeConstant(T);

    template< typename T >
    void loadChunkInto(Offset, Extent, std::shared_ptr< T >, double targetUnitSI = 0.0);
    template< typename T >
    std::unique_ptr< T, std::function< void(T*) > >
    loadChunk(Offset, Extent, double targetUnitSI = 0.0);
    template< typename T >
    void storeChunk(Offset, Extent, std::shared_ptr< T >);

    constexpr static char const * const SCALAR = "\tScalar";

private:
    void flush(std::string const&);
    virtual void read();
};  //RecordComponent

class MeshRecordComponent : public RecordComponent
{
    template<
            typename T,
            typename T_key
    >
    friend class Container;
    friend class Mesh;

private:
    MeshRecordComponent();
    void read() override;

public:
    template< typename T >
    std::vector< T > position() const;
    template< typename T >
    MeshRecordComponent& setPosition(std::vector< T >);

    template< typename T >
    MeshRecordComponent& makeConstant(T);
};


template< typename T >
inline RecordComponent&
RecordComponent::makeConstant(T value)
{
    if( written )
        throw std::runtime_error("A recordComponent can not (yet) be made constant after it has been written.");

    m_constantValue = Attribute(value);
    m_isConstant = true;
    return *this;
}

/*
template< typename T >
inline void
RecordComponent::loadChunkInto(Offset o, Extent e, std::shared_ptr<T> data, double targetUnitSI)
{
    throw std::runtime_error("loadChunkInto not yet implemented");
}
 */

template< typename T >
inline std::unique_ptr< T, std::function< void(T*) > >
RecordComponent::loadChunk(Offset o, Extent e, double targetUnitSI)
{
    if( targetUnitSI != 0. )
        throw std::runtime_error("unitSI scaling during chunk loading not yet implemented");
    Datatype dtype = determineDatatype(std::shared_ptr< T >());
    if( dtype != getDatatype() )
        throw std::runtime_error("Type conversion during chunk loading not implemented yet");
    uint8_t dim = getDimensionality();
    if( e.size() != dim || o.size() != dim )
        throw std::runtime_error("Dimensionality of chunk and dataset do not match.");
    Extent dse = getExtent();
    for( uint8_t i = 0; i < dim; ++i )
        if( dse[i] < o[i] + e[i] )
            throw std::runtime_error("Chunk does not reside inside dataset (Dimension on index " + std::to_string(i)
                                     + " - DS: " + std::to_string(dse[i])
                                     + " - Chunk: " + std::to_string(o[i] + e[i])
                                     + ")");

    size_t numPoints = 1;
    for( auto const& dimensionSize : e )
        numPoints *= dimensionSize;

    void* data = nullptr;
    switch( getDatatype() )
    {
        using DT = Datatype;
        case DT::DOUBLE:
            data = new double[numPoints];
            break;
        case DT::FLOAT:
            data = new float[numPoints];
            break;
        case DT::INT16:
            data = new int16_t[numPoints];
            break;
        case DT::INT32:
            data = new int32_t[numPoints];
            break;
        case DT::INT64:
            data = new int64_t[numPoints];
            break;
        case DT::UINT16:
            data = new uint16_t[numPoints];
            break;
        case DT::UINT32:
            data = new uint32_t[numPoints];
            break;
        case DT::UINT64:
            data = new uint64_t[numPoints];
            break;
        case DT::CHAR:
            data = new char[numPoints];
            break;
        case DT::UCHAR:
            data = new unsigned char[numPoints];
            break;
        case DT::BOOL:
            data = new bool[numPoints];
            break;
        case DT::UNDEFINED:
        default:
            throw std::runtime_error("Unknown Attribute datatype");
    }

    if( m_isConstant )
    {
        Parameter< Operation::READ_ATT > attribute_parameter;
        attribute_parameter.name = "value";
        IOHandler->enqueue(IOTask(this, attribute_parameter));
        IOHandler->flush();
        T* ptr = static_cast< T* >(data);
        T value = Attribute(*attribute_parameter.resource).get< T >();
        std::fill(ptr, ptr + numPoints, value);
    } else
    {
        Parameter< Operation::READ_DATASET > chunk_parameter;
        chunk_parameter.offset = o;
        chunk_parameter.extent = e;
        chunk_parameter.dtype = getDatatype();
        chunk_parameter.data = data;
        IOHandler->enqueue(IOTask(this, chunk_parameter));
        IOHandler->flush();
    }

    T* ptr = static_cast< T* >(data);
    auto deleter = [](T* p){ delete[] p; p = nullptr; };
    return std::unique_ptr< T, decltype(deleter) >(ptr, deleter);
}

template< typename T >
inline void
RecordComponent::storeChunk(Offset o, Extent e, std::shared_ptr<T> data)
{
    if( m_isConstant )
        throw std::runtime_error("Chunks can not be written for a constant RecordComponent.");
    Datatype dtype = determineDatatype(data);
    if( dtype != getDatatype() )
        throw std::runtime_error("Datatypes of chunk and dataset do not match.");
    uint8_t dim = getDimensionality();
    if( e.size() != dim || o.size() != dim )
        throw std::runtime_error("Dimensionality of chunk and dataset do not match.");
    Extent dse = getExtent();
    for( uint8_t i = 0; i < dim; ++i )
        if( dse[i] < o[i] + e[i] )
            throw std::runtime_error("Chunk does not reside inside dataset (Dimension on index " + std::to_string(i)
                                     + " - DS: " + std::to_string(dse[i])
                                     + " - Chunk: " + std::to_string(o[i] + e[i])
                                     + ")");

    Parameter< Operation::WRITE_DATASET > chunk_parameter;
    chunk_parameter.offset = o;
    chunk_parameter.extent = e;
    chunk_parameter.dtype = dtype;
    /* std::static_pointer_cast correctly reference-counts the pointer */
    chunk_parameter.data = std::static_pointer_cast< void >(data);
    m_chunks.push(IOTask(this, chunk_parameter));
}

template< typename T >
std::vector< T >
MeshRecordComponent::position() const
{ return readVectorFloatingpoint< T >("position"); }

template< typename T >
inline MeshRecordComponent&
MeshRecordComponent::makeConstant(T value)
{
    RecordComponent::makeConstant(value);
    return *this;
}