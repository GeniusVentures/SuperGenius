//---------------------------------------------------------------------
// File:		Singleton.hpp
// Description:	template for Singleton objects
// Created:		11/30/03
// Author:		Kenneth L. Hurley
/// \note   Elemental Engine
///         Copyright (C) 2013 Social Systems Technology, Inc.
///
///         This code is redistributable under the terms of the EE License.
///
///         This code is distributed without warranty or implied warranty of
///			merchantability or fitness for a particular purpose. See the 
///			EE License for more details.
///
///         You should have received a copy of the EE License along with this
///			code; If not, write to Social Systems Technology, Inc.,
///			109 East 17th Street Suite 4210 Cheyenne, WY 82001 USA
//---------------------------------------------------------------------

#ifndef SINGLETON_H
#define SINGLETON_H

//
// this code is so that object can register themselves with
// the system.
//

template <class T>
class CSingleton 
{
private:
    static bool isInitialized;
    static T *_instance;                  // first initialize memory without constructor
public:
	static T *Instance()
	{
        // start lock for multithreading here
        if (_instance == NULL)
        {
            _instance = new T(_instance);
        }
        
        if (!isInitialized)
        {
            isInitialized = true;
            new (_instance) T();
        }
		return _instance;
	}; 

private: 
	CSingleton() {}; 
	~CSingleton() {};
    // disable copy & assignment
    CSingleton( CSingleton const&);
    CSingleton& operator=( CSingleton const&);
};

template <class T>
bool CSingleton<T>::isInitialized = false;

template <class T>
T *CSingleton<T>::_instance = NULL;

#define SINGLETONINSTANCE(T)						\
	CSingleton< T >::Instance()

#define SINGLETON(T)								\
	protected:										\
		friend class CSingleton< T >;				\
        T(T *) {}                                   \
		T(){}

#define SINGLETONCONSTRUCTOROVERRIDE(T)				\
	protected:										\
		friend class CSingleton< T >;               \
        T(T *) {}


#endif	// #ifndef SINGLETON_H