/*--------------------------------------------------------------------------*\
License
    This file is part of solids4foam.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with solids4foam.  If not, see <http://www.gnu.org/licenses/>.

Application
    setFibreField

Description
    Set the initial fibre field for cardiac simulations.

    The utility currently sets the fibre field for the Land et al. (2015)
    ellipsoidal ventricle benchmarks.

Author
    Philip Cardiff, UCD.

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "transform.H"
#include "unitConversion.H"


// * * * * * * * * * * * * * H Functions  * * * * * * * * * * * * * * * * //

// Arostica et al. (2025) approach
// The procedure is ported from the python ellipsoid_fiber_generation.py script
// available in the cardiac_benchmark_toolkit repository shared on github
void FibreExpression
(
    const scalar x,
    const scalar y,
    const scalar z,
    const scalar rl,
    const scalar rs,
    const scalar fibreAngle,
    scalar& a,
    scalar& b,
    scalar& uu,
    scalar& vv,
    vector& f
)
{
    // a
    a = Foam::sqrt((y * y) + (z * z))/rs;

    //b
    b = x / rl;

    //uu
    uu = Foam::atan2(a, b);

    // vv
    if (mag(uu) < 1e-7)
    {
        vv = 0;
    }
    else
    {
        vv = Foam::constant::mathematical::pi - Foam::atan2(z, -y);
    }

    const vector derivDir
    (
        Foam::sin(fibreAngle), Foam::cos(fibreAngle), 0.0
    );

    vector Mcol0
    (
        -rl*std::sin(uu),
        rs*std::cos(uu)*std::cos(vv),
        rs*std::cos(uu)*std::sin(vv)
    );
    Mcol0 /= mag(Mcol0);

    vector Mcol1
    (
        0,
        -rs*std::sin(uu)*std::sin(vv),
        rs*std::sin(uu)*std::cos(vv)
    );
    Mcol1 /= mag(Mcol1);

    const tensor M
    (
        Mcol0.x(), Mcol1.x(), 0,
        Mcol0.y(), Mcol1.y(), 0,
        Mcol0.z(), Mcol1.z(), 0
    );

    f = M & derivDir;
    f /= mag(f);
}



// * * * * * * * * * * * * * H Functions  * * * * * * * * * * * * * * * * //

// Land et al. (2015) approach
// The procedure is ported from the Matlab xyz_to_fiber.m script available
// in the mechbench repository shared on bitbucket
void xyzToFibre
(
    const scalar x,
    const scalar y,
    const scalar z,
    const scalar rl,
    const scalar rs,
    const scalar fibreAngle,
    scalar& uu,
    scalar& vv,
    scalar& q,
    vector& f
)
{
    // vv
    vv = Foam::atan2(-y, -x);

    // q
    if (mag(Foam::cos(vv)) > 1e-6)
    {
        q = x/Foam::cos(vv);
    }
    else
    {
        q = y/Foam::sin(vv);
    }

    // u
    // Careful: acos is only defined between -1 and +1. We need to bound the
    // argument to guard against round-off error
    uu = Foam::acos(max(min(z/rl, 1.0), -1.0));

    if (uu > 0)
    {
        uu = -uu;
    }

    const vector derivDir
    (
        Foam::sin(fibreAngle), Foam::cos(fibreAngle), 0.0
    );

    vector Mcol0
    (
        rs*std::cos(uu)*std::cos(vv),
        rs*std::cos(uu)*std::sin(vv),
        -rl*std::sin(uu)
    );
    Mcol0 /= mag(Mcol0);

    vector Mcol1
    (
        -rs*std::sin(uu)*std::sin(vv),
        rs*std::sin(uu)*std::cos(vv),
        0
    );
    Mcol1 /= mag(Mcol1);

    const tensor M
    (
        Mcol0.x(), Mcol1.x(), 0,
        Mcol0.y(), Mcol1.y(), 0,
        Mcol0.z(), Mcol1.z(), 0
    );

    f = M & derivDir;
    f /= mag(f);
}


// * * * * * * * * * * * * * Main Program  * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    #include "addRegionOption.H"
    #include "setRootCase.H"
    #include "createTime.H"
    #include "createNamedMesh.H"

    // Read the transmural distance field
    Info<< "Reading t" << endl;
    volScalarField t
    (
        IOobject
        (
            "t",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh
    );

    Info<< "Solving the diffusion equation for t" << endl;
    fvScalarMatrix tEqn(fvm::laplacian(t));
    tEqn.solve();

    Info<< "Writing t" << endl;
    t.write();

    // Read the input dict for specifying user-changeable parameters
    Info<< "Reading system/setFibreFieldDict" << endl;
    IOdictionary dict
    (
        IOobject
        (
            "setFibreFieldDict",
            runTime.system(),
            runTime,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        )
    );

    // Examples for looking up quantities from a dict
    // const Switch Land2015(dict.lookup("Land2015"));
    // const vector myVec(dict.lookup("myVec"));
    // const word myWord(dict.lookup("banana"));
    // const int myInt(dict.lookupOrDefault<int("myInt", 4));

    //Adding the paper model variable from the dict
    const word paperModel(dict.lookupOrDefault<word>("paperModel", "Land2015"));

    
    // Fibre angle at the inner surface
    const scalar fibreAngleEndo
    (
        dict.lookupOrDefault<scalar>("fibreAngleEndo", 90.0)
    );

    // Fibre angle at the puter surface
    const scalar fibreAngleEpi
    (
        dict.lookupOrDefault<scalar>("fibreAngleEpi", -90.0)
    );

    // Alpha field
    const scalar pi = Foam::constant::mathematical::pi;
    const volScalarField alphaRadians
    (
        "alphaRadians",
        (fibreAngleEndo + (fibreAngleEpi - fibreAngleEndo)*t)*pi/180.0
    );

    Info<< "Writing alphaRadians" << endl;
    alphaRadians.write();


    // Read geometry parameters
    const scalar rShortEndo
    (
        dict.lookupOrDefault<scalar>("rShortEndo", 0.007)
    );
    const scalar rShortEpi
    (
        dict.lookupOrDefault<scalar>("rShortEpi", 0.01)
    );
    const scalar rLongEndo
    (
        dict.lookupOrDefault<scalar>("rLongEndo", 0.017)
    );
    const scalar rLongEpi
    (
        dict.lookupOrDefault<scalar>("rLongEpi", 0.02)
    );

    // Creating rs and rs

    // Constants as defined from Eq's (10-11)
    // with r_long_endo = 9x10^-2
    //from Eq (16) of the paper we get rs and rl
    const volScalarField rs
    (
        "rs",
        // 0.007 + 0.003*t // careful: use metres!
        rShortEndo + (rShortEpi - rShortEndo)*t
    );
    Info<< "Writing rs" << endl;
    rs.write();

    const volScalarField rl
    (
        "rl",
        // 0.017 + 0.003*t // careful: use metres!
        rLongEndo + (rLongEpi - rLongEndo)*t
    );
    Info<< "Writing rl" << endl;
    rl.write();


    volScalarField a
    (
        IOobject
        (
            "a",                    // Name of the field
            runTime.timeName(),     // Time directory (e.g., "0", "1", etc.)
            mesh,                   // Mesh to associate the field with
            IOobject::NO_READ,      // Do not read from disk (no existing file)
            IOobject::AUTO_WRITE    // Write the field automatically to disk
        ),
        mesh,
        dimensionedScalar("a", dimless, 0.0) // Mesh to define the field on
    );

    volScalarField b
    (
        IOobject
        (
            "b",                    // Name of the field
            runTime.timeName(),     // Time directory (e.g., "0", "1", etc.)
            mesh,                   // Mesh to associate the field with
            IOobject::NO_READ,      // Do not read from disk (no existing file)
            IOobject::AUTO_WRITE    // Write the field automatically to disk
        ),
        mesh,
        dimensionedScalar("b", dimless, 0.0) // Mesh to define the field on
    );

    // Initialising uu
    volScalarField uu
    (
        IOobject
        (
            "uu",                    // Name of the field
            runTime.timeName(),     // Time directory (e.g., "0", "1", etc.)
            mesh,                   // Mesh to associate the field with
            IOobject::NO_READ,      // Do not read from disk (no existing file)
            IOobject::AUTO_WRITE    // Write the field automatically to disk
        ),
        mesh,
        dimensionedScalar("uu", dimless, 0.0) // Mesh to define the field on
    );

    // Initialising vv
    volScalarField vv
    (
        IOobject
        (
            "vv",                    // Name of the field
            runTime.timeName(),     // Time directory (e.g., "0", "1", etc.)
            mesh,                   // Mesh to associate the field with
            IOobject::NO_READ,      // Do not read from disk (no existing file)
            IOobject::AUTO_WRITE    // Write the field automatically to disk
        ),
        mesh,
        dimensionedScalar("vv", dimless, 0.0)
    );

    // Initialising q
    volScalarField q
    (
        IOobject
        (
            "q",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimensionedScalar("q", dimless, 0.0)
    );

    // Initialising the fibres
    volVectorField f0
    (
        IOobject
        (
            "f0",        // Field name
            mesh.time().timeName(), // Time directory (still required but not used)
            mesh,             // Mesh reference
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh,
        vector::zero
    );

    // Take references for efficiency and brevity
    const volVectorField& C = mesh.C();
    const vectorField& CI = C;

    // Looping through an updating the mu and theta values

    if(paperModel == "Land2015"){
      forAll(CI, cellI)
        {

          xyzToFibre
            (
             CI[cellI].x(),
             CI[cellI].y(),
             CI[cellI].z(),
             rl[cellI],
             rs[cellI],
             alphaRadians[cellI],
             uu[cellI],
             vv[cellI],
             q[cellI],
             f0[cellI]
             );

        }

      //Doing the same for the boundaries
      forAll(C.boundaryField(), patchI)
        {
          // Loop over faces in patch
          const vectorField& CP = C.boundaryField()[patchI];
          const scalarField& rlP = rl.boundaryField()[patchI];
          const scalarField& rsP = rs.boundaryField()[patchI];
          const scalarField& alphaRadiansP = alphaRadians.boundaryField()[patchI];
          scalarField& uuP = uu.boundaryFieldRef()[patchI];
          scalarField& vvP = vv.boundaryFieldRef()[patchI];
          scalarField& qP = q.boundaryFieldRef()[patchI];
          vectorField& f0P = f0.boundaryFieldRef()[patchI];
          forAll(CP, faceI)
            {
              xyzToFibre
                (
                 CP[faceI].x(),
                 CP[faceI].y(),
                 CP[faceI].z(),
                 rlP[faceI],
                 rsP[faceI],
                 alphaRadiansP[faceI],
                 uuP[faceI],
                 vvP[faceI],
                 qP[faceI],
                 f0P[faceI]
                 );
            }
        }
    }

    if(paperModel == "Arostica2025"){
      forAll(CI, cellI)
        {

          FibreExpression
            (
             CI[cellI].x(),
             CI[cellI].y(),
             CI[cellI].z(),
             rl[cellI],
             rs[cellI],
             alphaRadians[cellI],
             a[cellI],
             b[cellI],
             uu[cellI],
             vv[cellI],
             f0[cellI]
             );

        }

      //Doing the same for boundaries
      forAll(C.boundaryField(), patchI)
        {
          // Loop over faces in patch
          const vectorField& CP = C.boundaryField()[patchI];
          const scalarField& rlP = rl.boundaryField()[patchI];
          const scalarField& rsP = rs.boundaryField()[patchI];
          const scalarField& alphaRadiansP = alphaRadians.boundaryField()[patchI];
          scalarField& aP = a.boundaryFieldRef()[patchI];
          scalarField& bP = b.boundaryFieldRef()[patchI];
          scalarField& uuP = uu.boundaryFieldRef()[patchI];
          scalarField& vvP = vv.boundaryFieldRef()[patchI];
          vectorField& f0P = f0.boundaryFieldRef()[patchI];
          forAll(CP, faceI)
            {
              FibreExpression
                (
                 CP[faceI].x(),
                 CP[faceI].y(),
                 CP[faceI].z(),
                 rlP[faceI],
                 rsP[faceI],
                 alphaRadiansP[faceI],
                 aP[faceI],
                 bP[faceI],
                 uuP[faceI],
                 vvP[faceI],
                 f0P[faceI]
                 );
            }
        }
    }

    Info<< "Writing a" << nl
        << "    fibreAngleEndo = " << fibreAngleEndo << nl;
    a.write();
    Info<< "Writing b" << nl;
    b.write();
    Info<< "Writing uu" << nl
        << "    max(uu) = " << max(uu) << nl
        << "    min(uu) = " << min(uu) << endl;
    uu.write();
    Info<< "Writing vv" << nl
        << "    max(vv) = " << max(uu) << nl
        << "    min(vv) = " << min(uu) << endl;
    vv.write();
    Info<< "Writing q" << nl
        << "    max(q) = " << max(uu) << nl
        << "    min(q) = " << min(uu) << endl;
    q.write();

    // Write f0
    Info<< "Writing " << f0.name() << nl << endl;
    f0.write();

    Info<< "Done" << nl << endl;

    return 0;
}


// ************************************************************************* //
