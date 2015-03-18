Name:           gst-droid
Summary:        GStreamer droid plug-in contains elements using the Android HAL
Version:        1.0.0
Release:        1
Group:          Applications/Multimedia
License:        LGPL2.1+
URL:            https://github.com/foolab/gst-droid
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  pkgconfig(gstreamer-plugins-base-1.0)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-base-1.0)
BuildRequires:  pkgconfig(gstreamer-video-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-bad-1.0)
BuildRequires:  pkgconfig(gstreamer-tag-1.0)
BuildRequires:  nemo-gstreamer1.0-interfaces-devel
BuildRequires:  pkgconfig(libexif)
BuildRequires:  libtool
BuildRequires:  gettext

%description
GStreamer droid plug-in contains elements using the Android HAL

%package -n devel
Summary: gstreamer interface used for video rendering devel package
Group: Applications/Multimedia
Requires:  gst-droid = %{version}-%{release}

%description -n devel
%{summary}

%prep
%setup -q

%build
./autogen.sh
./configure --disable-static --prefix=%_prefix --sysconfdir=%{_sysconfdir}
make

%install
find . -name "*.la" -exec rm -f {} \;
%make_install

%files
%defattr(-,root,root,-)
%{_libdir}/gstreamer-1.0/*.so
%{_libdir}/*.so.*
%{_sysconfdir}/%name/

%files -n devel
%defattr(-,root,root,-)
%{_includedir}/gstreamer-1.0/gst/
%{_libdir}/*.so

