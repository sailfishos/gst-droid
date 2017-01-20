%define majorminor 1.0
%define gstreamer gstreamer

Name:           %{gstreamer}%{majorminor}-droid
Summary:        GStreamer droid plug-in contains elements using the Android HAL
Version:        1.0.0
Release:        1
Group:          Applications/Multimedia
License:        LGPLv2.1+
URL:            https://github.com/sailfishos/gst-droid
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(gstreamer-plugins-base-1.0)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-base-1.0)
BuildRequires:  pkgconfig(gstreamer-video-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-bad-1.0)
BuildRequires:  pkgconfig(gstreamer-tag-1.0)
BuildRequires:  pkgconfig(nemo-gstreamer-interfaces-1.0)
BuildRequires:  pkgconfig(libexif)
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  gettext
BuildRequires:  droidmedia-devel
BuildRequires:  pkgconfig(libandroid-properties)
Requires:       droidmedia >= 0.20170120.0

%description
GStreamer droid plug-in contains elements using the Android HAL

%package devel
Summary: GStreamer droid plug-in contains elements using the Android HAL devel package
Requires:  %{gstreamer}%{majorminor}-droid = %{version}-%{release}

%description devel
%{summary}

%package tools
Summary: Tools for gst-droid
Requires:  %{gstreamer}%{majorminor}-droid = %{version}-%{release}

%description tools
%{summary}

%prep
%setup -q

%build
NOCONFIGURE=1 ./autogen.sh
%configure --disable-static --prefix=%_prefix --sysconfdir=%{_sysconfdir}
make %{?jobs:-j%jobs}

%install
%make_install

# Clean out files that should not be part of the rpm.
rm -rf $RPM_BUILD_ROOT%{_sysconfdir}/gst-droid

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/gstreamer-%{majorminor}/*.so
%{_libdir}/*.so.*

%files devel
%defattr(-,root,root,-)
%{_includedir}/gstreamer-%{majorminor}/gst/
%{_libdir}/*.so

%files tools
%defattr(-,root,root,-)
%{_bindir}/mk-cam-conf
%{_bindir}/record-video
%{_bindir}/dump-resolutions
