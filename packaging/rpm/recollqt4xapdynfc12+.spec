Name:           recoll
Version:        1.13.04
Release:        1%{?dist}
Summary:        Desktop full text search tool with a qt gui

Group:          Applications/Databases
License:        GPL
URL:            http://www.recoll.org/
Source0:        http://www.recoll.org/recoll-1.13.04.tar.gz 
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

# Not sure how easy it is to find a xapian-core rpm. Will be easier to
# build by hand for many. Run time uses a static link to xapian, doesnt
# depend on libxapian.so
BuildRequires:  qt-devel xapian-core-devel zlib-devel desktop-file-utils
Requires:       qt xapian-core-libs zlib

%description
Recoll is a personal full text search package for Linux, FreeBSD and
other Unix systems. It is based on a very strong backend (Xapian), for
which it provides an easy to use, feature-rich, easy administration
interface.

%prep
%setup -q
QMAKE=qmake-qt4
export QMAKE
%build
%configure
make %{?_smp_mflags} 

%install
rm -rf $RPM_BUILD_ROOT
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_bindir}/*
%{_datadir}/%{name}
%{_datadir}/applications/recoll-searchgui.desktop
%{_datadir}/icons/hicolor/48x48/apps/recoll.png
%{_datadir}/pixmaps/recoll.png
%{_mandir}/man1/recoll*
%{_mandir}/man5/recoll*
%doc


%changelog
* Thu Apr 14 2010 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.13.04-1
- Update to release 1.13.04
* Thu Jan 07 2010 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.13.01-1
- Update to release 1.13.01
* Wed Oct 28 2009 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.12.3-1
- Update to release 1.12.3
* Thu Jan 29 2009 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.12.0-1
- Update to release 1.12.0
* Mon Oct 13 2008 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.11.0-1
- Update to release 1.11.0
* Fri Sep 12 2008 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.10.6-1
- Update to release 1.10.6
* Thu May 27 2008 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.10.2-1
- Update to release 1.10.2
* Thu Jan 31 2008 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.10.1-1
- Update to release 1.10.1
* Wed Nov 21 2007 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.10.0-1
- Update to release 1.10.0
* Tue Sep 11 2007 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.9.0-1
- Update to release 1.9.0
* Tue Mar 6 2007 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.8.1-1
- Update to release 1.8.1
* Mon Jan 15 2007 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.7.5-1
- Update to release 1.7.5
* Mon Jan 08 2007 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.7.3-1
- Update to release 1.7.3
* Tue Nov 28 2006 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.6.1-1
- Update to release 1.6.0
* Mon Nov 20 2006 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.5.11-1
- Update to release 1.5.11
* Mon Oct 2 2006 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.5.2-1
- Update to release 1.5.2
* Fri Mar 31 2006 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.3.2-1
- Update to release 1.3.1
* Wed Feb  1 2006 Jean-Francois Dockes <jean-francois.dockes@wanadoo.fr> 1.2.0-1
- Initial packaging
